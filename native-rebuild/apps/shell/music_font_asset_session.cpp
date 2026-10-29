#include "apps/shell/music_font_asset_session.h"

#include "vendor/nlohmann/json.hpp"

#include <bcrypt.h>
#include <shobjidl.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <utility>

namespace lume::shell {
namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;
constexpr std::uint64_t MaxFile = 50ULL * 1024 * 1024;
constexpr std::uint64_t MaxTotal = 200ULL * 1024 * 1024;
constexpr std::size_t MaxCount = 8, MaxMetadata = 16384;
constexpr std::uint64_t MaxRequest = 9007199254740991ULL;
constexpr char Prefix[] = "LumeImportedFont_";

struct Failure { const char* code; };
void Require(bool ok, const char* code = "save") { if (!ok) throw Failure{code}; }
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
template<class T> struct Com {
    T* value = nullptr;
    ~Com() { if (value) value->Release(); }
};
bool Missing() { return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND; }
fs::path LocalPath(const fs::path& source) {
    Require(!source.empty() && source.native().find(L'\0') == std::wstring::npos, "invalid");
    const auto path = fs::absolute(source).lexically_normal();
    const auto text = path.wstring();
    Require(text.size() >= 3 && text.size() < 220 && text[1] == L':' &&
        ((text[0] >= L'A' && text[0] <= L'Z') || (text[0] >= L'a' && text[0] <= L'z')) &&
        (text[2] == L'\\' || text[2] == L'/') && text.find(L':', 2) == text.npos, "invalid");
    for (const auto& part : path.relative_path()) {
        const auto name = part.wstring();
        Require(!name.empty() && name.back() != L'.' && name.back() != L' ', "invalid");
    }
    return path;
}
struct Directories {
    std::vector<std::unique_ptr<Handle>> handles;
    bool Open(const fs::path& directory, bool create) {
        auto path = directory.root_path();
        if (!Hold(path, create)) return false;
        for (const auto& part : directory.relative_path()) {
            path /= part;
            if (!Hold(path, create)) return false;
        }
        return true;
    }
    bool Hold(const fs::path& path, bool create) {
        auto file = std::make_unique<Handle>();
        file->value = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file->value == INVALID_HANDLE_VALUE && Missing()) {
            if (!create) return false;
            Require(CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS);
            file->value = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        }
        BY_HANDLE_FILE_INFORMATION info{};
        Require(file->value != INVALID_HANDLE_VALUE && GetFileInformationByHandle(file->value, &info) &&
            (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT));
        // Checked ancestors stay open without delete sharing until publication.
        handles.push_back(std::move(file));
        return true;
    }
};
std::uint64_t Regular(HANDLE file) {
    BY_HANDLE_FILE_INFORMATION info{};
    Require(file != INVALID_HANDLE_VALUE && GetFileInformationByHandle(file, &info) &&
        !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) &&
        info.nNumberOfLinks == 1);
    return (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
}
std::string Utf8(std::wstring_view text) {
    Require(!text.empty() && text.size() <= 4096, "invalid");
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    Require(count > 0, "invalid");
    std::string result(static_cast<std::size_t>(count), '\0');
    Require(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), result.data(), count, nullptr, nullptr) == count, "invalid");
    return result;
}
bool Label(const std::string& text) {
    if (text.empty() || text.size() > 128) return false;
    std::array<wchar_t, 129> wide{};
    const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), wide.data(), static_cast<int>(wide.size()));
    return length > 0 && std::none_of(wide.begin(), wide.begin() + length, [](wchar_t c) {
        return c < 32 || (c >= 127 && c <= 159) || c == L'\\' || c == L'/' || c == L':';
    });
}
bool Id(const std::string& id) {
    return id.size() == 32 && id.find_first_not_of("0123456789abcdef") == id.npos;
}
std::string RandomId() {
    std::array<unsigned char, 16> bytes{};
    Require(BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0);
    std::string result;
    for (auto b : bytes) { result += "0123456789abcdef"[b >> 4]; result += "0123456789abcdef"[b & 15]; }
    return result;
}
bool Extension(const std::string& ext) {
    return ext == "ttf" || ext == "otf" || ext == "woff" || ext == "woff2";
}
std::uint32_t U32(const std::string& data, std::size_t at) {
    Require(at + 4 <= data.size(), "invalid");
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(data[at])) << 24) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(data[at + 1])) << 16) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(data[at + 2])) << 8) |
        static_cast<unsigned char>(data[at + 3]);
}
std::uint32_t U16(const std::string& data, std::size_t at) {
    Require(at + 2 <= data.size(), "invalid");
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(data[at])) << 8) |
        static_cast<unsigned char>(data[at + 1]);
}
void CheckFont(HANDLE file, const std::string& extension, std::uint64_t size) {
    Require(size > 12 && size <= MaxFile, "invalid");
    // Only container headers/directories are parsed here. FontFace.load is the
    // mandatory semantic decoder before the renderer commits a preference.
    const auto count = static_cast<DWORD>(std::min<std::uint64_t>(size, 44 + 4096 * 20));
    std::string data(count, '\0');
    LARGE_INTEGER start{};
    Require(SetFilePointerEx(file, start, nullptr, FILE_BEGIN), "invalid");
    DWORD read = 0;
    Require(ReadFile(file, data.data(), count, &read, nullptr) && read == count, "invalid");
    const auto signature = U32(data, 0);
    if (extension == "ttf" || extension == "otf") {
        Require(extension == "otf" ? signature == 0x4f54544fU :
            signature == 0x00010000U || signature == 0x74727565U, "invalid");
        const auto tables = U16(data, 4);
        const auto header = 12 + tables * 16;
        Require(tables && tables <= 4096 && header <= size, "invalid");
        for (std::size_t i = 0; i < tables; ++i) {
            const auto offset = U32(data, 12 + i * 16 + 8);
            const auto length = U32(data, 12 + i * 16 + 12);
            Require(offset >= header && offset <= size && length <= size - offset, "invalid");
        }
    } else {
        const bool woff = extension == "woff";
        Require(signature == (woff ? 0x774f4646U : 0x774f4632U) &&
            size >= (woff ? 44U : 48U) && U32(data, 8) == size && U16(data, 14) == 0, "invalid");
        const auto flavor = U32(data, 4);
        Require(flavor == 0x00010000U || flavor == 0x4f54544fU || flavor == 0x74727565U, "invalid");
        const auto tables = U16(data, 12);
        Require(tables && tables <= 4096 && U32(data, 16) >= 12 + tables * 16 &&
            U32(data, 16) <= MaxFile, "invalid");
        if (woff) {
            const auto header = 44 + tables * 20;
            Require(header <= size, "invalid");
            for (std::size_t i = 0; i < tables; ++i) {
                const auto offset = U32(data, 44 + i * 20 + 4);
                const auto length = U32(data, 44 + i * 20 + 8);
                Require(offset >= header && offset <= size && length <= size - offset &&
                    length <= U32(data, 44 + i * 20 + 12), "invalid");
            }
        } else Require(U32(data, 20) > 0 && U32(data, 20) <= size - 48, "invalid");
    }
    Require(SetFilePointerEx(file, start, nullptr, FILE_BEGIN), "invalid");
}
bool Keys(const Json& j, std::initializer_list<const char*> names) {
    return j.is_object() && j.size() == names.size() &&
        std::all_of(names.begin(), names.end(), [&](const char* name) { return j.contains(name); });
}
std::optional<Json> Command(std::wstring_view text) {
    if (text.empty() || text.size() > 2048 || text.find(L'\0') != text.npos) return {};
    bool invalid = false;
    std::set<std::string> keys;
    auto j = Json::parse(Utf8(text), [&](int depth, Json::parse_event_t event, Json& value) {
        if (depth > 1) { invalid = true; return false; }
        if (event == Json::parse_event_t::key && !keys.insert(value.get<std::string>()).second) invalid = true;
        return true;
    }, false);
    if (invalid || !j.is_object() || !j.contains("version") || !j["version"].is_number_unsigned() ||
        j["version"] != 1 || j.value("scope", "") != "music-font-assets") return {};
    const auto action = j.value("action", "");
    if (action == "snapshot") return Keys(j, {"version", "scope", "action"}) ? std::optional{j} : std::nullopt;
    if (((action == "choose" || action == "cancel") && !Keys(j, {"version", "scope", "action", "request"})) ||
        (action == "drop" && (!Keys(j, {"version", "scope", "action", "request", "id"}) ||
            !j["id"].is_string() || !Id(j["id"].get<std::string>()))) ||
        (action != "choose" && action != "drop" && action != "cancel")) return {};
    if (!j["request"].is_number_unsigned() || j["request"].get<std::uint64_t>() == 0 ||
        j["request"].get<std::uint64_t>() > MaxRequest) return {};
    return j;
}
std::wstring Reply(std::uint64_t request, const Json& assets, const char* error, const Json& candidate = nullptr) {
    const auto text = Json{{"version", 1}, {"type", "music-font-assets-state"}, {"request", request},
        {"error", error}, {"assets", assets}, {"candidate", candidate}}.dump(-1, ' ', true);
    return {text.begin(), text.end()};
}
fs::path File(const fs::path& directory, const Json& asset) {
    return directory / (asset.at("id").get<std::string>() + "." + asset.at("extension").get<std::string>());
}
void WriteAll(HANDLE file, const char* bytes, DWORD size) {
    DWORD written = 0;
    Require(WriteFile(file, bytes, size, &written, nullptr) && written == size);
}
void Destination(const fs::path& path) {
    Handle file(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (file.value == INVALID_HANDLE_VALUE && Missing()) return;
    Regular(file.value);
}
void Publish(const fs::path& root, const Json& assets) {
    const auto destination = root / "font-assets.json";
    Destination(destination);
    const auto temp = root / (RandomId() + ".tmp");
    bool created = false;
    try {
        {
            Handle file(CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            Require(file.value != INVALID_HANDLE_VALUE);
            created = true; Regular(file.value);
            const auto text = Json{{"version", 1}, {"assets", assets}}.dump();
            Require(text.size() <= MaxMetadata);
            WriteAll(file.value, text.data(), static_cast<DWORD>(text.size()));
            Require(FlushFileBuffers(file.value));
        }
        Destination(destination);
        Require(MoveFileExW(temp.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH));
    } catch (...) {
        if (created) DeleteFileW(temp.c_str());
        throw;
    }
}
}  // namespace

struct MusicFontAssetSession::Impl {
    explicit Impl(MusicFontAssetSessionOptions o) : options(std::move(o)) {}
    MusicFontAssetSessionOptions options;
    Json assets = Json::array();
    std::set<std::string> imported;
    std::map<std::uint64_t, std::string> candidates;
    bool stopped = false, choosing = false;
    std::uint64_t generation = 0, choosingRequest = 0;
    IFileOpenDialog* dialog = nullptr;

    fs::path Root() const { return LocalPath(options.root); }
    Json Load(const fs::path& root) {
        Directories dirs;
        if (!dirs.Open(root, false)) return Json::array();
        Handle metadata(CreateFileW((root / "font-assets.json").c_str(), GENERIC_READ, FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (metadata.value == INVALID_HANDLE_VALUE && Missing()) return Json::array();
        const auto size = Regular(metadata.value);
        Require(size && size <= MaxMetadata, "load");
        std::string text(static_cast<std::size_t>(size), '\0');
        DWORD read = 0;
        Require(ReadFile(metadata.value, text.data(), static_cast<DWORD>(size), &read, nullptr) && read == size, "load");
        std::array<std::set<std::string>, 4> keys;
        const auto value = Json::parse(text, [&](int depth, Json::parse_event_t event, Json& item) {
            Require(depth >= 0 && depth < static_cast<int>(keys.size()), "load");
            if (event == Json::parse_event_t::object_start) keys[static_cast<std::size_t>(depth)].clear();
            if (event == Json::parse_event_t::key)
                Require(depth > 0 && keys[static_cast<std::size_t>(depth - 1)].insert(item.get<std::string>()).second,
                    "load");
            return true;
        });
        Require(Keys(value, {"version", "assets"}) && value.at("version").is_number_unsigned() &&
            value.at("version") == 1 && value.at("assets").is_array() && value.at("assets").size() <= MaxCount, "load");
        const auto result = value.at("assets");
        if (!result.empty()) Require(dirs.Open(root / "fonts", false), "load");
        std::set<std::string> ids;
        std::uint64_t total = 0;
        for (const auto& asset : result) {
            Require(Keys(asset, {"id", "family", "label", "extension", "size"}), "load");
            const auto id = asset.at("id").get<std::string>();
            const auto ext = asset.at("extension").get<std::string>();
            Require(Id(id) && ids.insert(id).second && asset.at("family") == std::string(Prefix) + id &&
                Label(asset.at("label").get<std::string>()) && Extension(ext) &&
                asset.at("size").is_number_unsigned(), "load");
            const auto length = asset.at("size").get<std::uint64_t>();
            Require(length && length <= MaxFile && length <= MaxTotal - total, "load");
            total += length;
            Handle font(CreateFileW(File(root / "fonts", asset).c_str(), GENERIC_READ, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            Require(Regular(font.value) == length, "load");
            CheckFont(font.value, ext, length);
        }
        return result;
    }
    fs::path Choose(HWND owner) {
        if (options.chooseFont) return options.chooseFont(owner);
        if (options.verification) return {};
        Com<IFileOpenDialog> picker;
        Require(SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&picker.value))), "load");
        const COMDLG_FILTERSPEC filters[] = {{L"Font files", L"*.woff2;*.woff;*.ttf;*.otf"}};
        Require(SUCCEEDED(picker.value->SetOptions(FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST |
            FOS_FORCEFILESYSTEM | FOS_DONTADDTORECENT | FOS_NOCHANGEDIR)) &&
            SUCCEEDED(picker.value->SetFileTypes(1, filters)), "load");
        dialog = picker.value;
        const auto shown = picker.value->Show(owner);
        dialog = nullptr;
        if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return {};
        Require(SUCCEEDED(shown), "load");
        Com<IShellItem> item;
        Require(SUCCEEDED(picker.value->GetResult(&item.value)), "load");
        PWSTR name = nullptr;
        Require(SUCCEEDED(item.value->GetDisplayName(SIGDN_FILESYSPATH, &name)) && name, "load");
        fs::path result;
        try { result = name; } catch (...) { CoTaskMemFree(name); throw; }
        CoTaskMemFree(name);
        return result;
    }
    Json Import(const fs::path& root, const fs::path& picked, std::uint64_t request) {
        const auto source = LocalPath(picked);
        std::string ext = Utf8(source.extension().wstring());
        if (!ext.empty()) ext.erase(ext.begin());
        for (auto& c : ext) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        const auto label = Utf8(source.stem().wstring());
        Require(Extension(ext) && Label(label), "invalid");
        Directories inputDirs;
        Require(inputDirs.Open(source.parent_path(), false), "invalid");
        Handle input(CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        const auto size = Regular(input.value);
        CheckFont(input.value, ext, size);
        Directories dirs;
        Require(dirs.Open(root / "fonts", true));
        // Count physical files too: crash-orphaned imports cannot bypass quotas.
        std::uint64_t total = 0;
        std::size_t count = 0;
        for (const auto& entry : fs::directory_iterator(root / "fonts")) {
            Require(++count <= MaxCount, "invalid");
            Handle file(CreateFileW(entry.path().c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
            const auto length = Regular(file.value);
            Require(length <= MaxTotal - total, "invalid"); total += length;
        }
        Require(count < MaxCount && assets.size() < MaxCount && size <= MaxTotal - total, "invalid");
        const auto id = RandomId();
        Json asset = {{"id", id}, {"family", std::string(Prefix) + id}, {"label", label},
            {"extension", ext}, {"size", size}};
        const auto destination = File(root / "fonts", asset);
        bool created = false, tracked = false;
        try {
            // Allocate bookkeeping before publishing. Nothing may delete an
            // already-published file if a later allocation fails.
            imported.insert(id);
            tracked = candidates.emplace(request, id).second;
            Require(tracked, "invalid");
            {
                Handle output(CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
                Require(output.value != INVALID_HANDLE_VALUE);
                created = true; Regular(output.value);
                std::array<char, 65536> buffer{};
                std::uint64_t left = size;
                while (left) {
                    const auto wanted = static_cast<DWORD>(std::min<std::uint64_t>(buffer.size(), left));
                    DWORD read = 0;
                    Require(ReadFile(input.value, buffer.data(), wanted, &read, nullptr) && read == wanted);
                    WriteAll(output.value, buffer.data(), read);
                    left -= read;
                }
                Require(FlushFileBuffers(output.value));
            }
            auto next = assets;
            next.push_back(asset);
            Publish(root, next);
            assets = std::move(next);
        } catch (...) {
            imported.erase(id);
            if (tracked) candidates.erase(request);
            if (created) DeleteFileW(destination.c_str());
            throw;
        }
        return asset;
    }
    void Drop(const fs::path& root, const std::string& id) {
        Require(imported.count(id) && options.selectedFamilies, "invalid");
        const auto it = std::find_if(assets.begin(), assets.end(), [&](const Json& a) { return a.at("id") == id; });
        Require(it != assets.end(), "invalid");
        const auto family = it->at("family").get<std::string>();
        const auto references = options.selectedFamilies();
        for (auto ref : references) {
            std::transform(ref.begin(), ref.end(), ref.begin(), [](unsigned char c) {
                return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            });
            auto lower = family;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            });
            Require(ref != lower, "invalid");
        }
        Directories dirs;
        Require(dirs.Open(root / "fonts", false));
        Handle file(CreateFileW(File(root / "fonts", *it).c_str(), DELETE | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        Regular(file.value);
        auto next = assets;
        next.erase(next.begin() + std::distance(assets.begin(), it));
        // Keep the handle open: a failed disposition must not change metadata,
        // and a failed publication must undo deletion before the handle closes.
        FILE_DISPOSITION_INFO disposition{TRUE};
        Require(SetFileInformationByHandle(file.value, FileDispositionInfo, &disposition, sizeof(disposition)));
        try { Publish(root, next); }
        catch (...) {
            disposition.DeleteFile = FALSE;
            Require(SetFileInformationByHandle(file.value, FileDispositionInfo, &disposition, sizeof(disposition)));
            throw;
        }
        assets = std::move(next);
        imported.erase(id);
        for (auto candidate = candidates.begin(); candidate != candidates.end();) {
            if (candidate->second == id) candidate = candidates.erase(candidate);
            else ++candidate;
        }
    }
    std::optional<std::wstring> HandleMessage(std::wstring_view text, HWND owner) {
        std::optional<Json> command;
        try { command = Command(text); } catch (...) { return {}; }
        if (!command) return {};
        const auto action = command->at("action").get<std::string>();
        const auto request = command->value("request", std::uint64_t{0});
        if (stopped) return Reply(request, assets, "cancelled");
        if (action == "cancel") {
            if (choosing && choosingRequest == request) {
                Cancel();
            } else if (const auto pending = candidates.find(request); pending != candidates.end()) {
                // The UI may abort after publication but before receiving the
                // reply. Only this request's unreferenced candidate is eligible.
                const auto id = pending->second;
                try {
                    const auto root = Root();
                    try { assets = Load(root); } catch (...) { throw Failure{"load"}; }
                    Drop(root, id);
                } catch (const Failure& failure) { return Reply(request, assets, failure.code); }
                catch (...) { return Reply(request, assets, "invalid"); }
            }
            return Reply(request, assets, "cancelled");
        }
        if (choosing) return Reply(request, assets, action == "snapshot" ? "" : "cancelled");
        fs::path root;
        try { root = Root(); assets = Load(root); }
        catch (...) { return Reply(request, Json::array(), "load"); }
        if (action == "snapshot") return Reply(0, assets, "");
        if (action == "choose" && candidates.count(request)) return Reply(request, assets, "invalid");
        try {
            if (action == "drop") {
                Drop(root, command->at("id").get<std::string>());
                return Reply(request, assets, "");
            }
            const auto serial = ++generation;
            choosing = true;
            choosingRequest = request;
            fs::path path;
            try { path = Choose(owner); }
            catch (...) { choosing = false; choosingRequest = 0; throw; }
            choosing = false;
            choosingRequest = 0;
            if (stopped || serial != generation || path.empty()) return Reply(request, assets, "cancelled");
            // A modal picker can pump messages. Reload the manifest before import.
            try { assets = Load(root); } catch (...) { throw Failure{"load"}; }
            const auto candidate = Import(root, path, request);
            return Reply(request, assets, "", candidate);
        } catch (const Failure& failure) { return Reply(request, assets, failure.code); }
        catch (...) { return Reply(request, assets, action == "drop" ? "invalid" : "save"); }
    }
    void Cancel() noexcept {
        ++generation;
        if (dialog) dialog->Close(HRESULT_FROM_WIN32(ERROR_CANCELLED));
    }
};

MusicFontAssetSession::MusicFontAssetSession(MusicFontAssetSessionOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}
MusicFontAssetSession::~MusicFontAssetSession() { Shutdown(); }
std::optional<std::wstring> MusicFontAssetSession::HandleMessage(std::wstring_view message, HWND owner) {
    return impl_->HandleMessage(message, owner);
}
fs::path MusicFontAssetSession::AssetDirectory() const {
    try { return impl_->Root() / "fonts"; } catch (...) { return {}; }
}
std::uint64_t MusicFontAssetSession::AssetSize(std::wstring_view filename) const {
    if (impl_->stopped || filename.size() < 36 || filename.size() > 38) return 0;
    for (const auto& asset : impl_->assets) {
        const auto expected = asset.at("id").get<std::string>() + "." + asset.at("extension").get<std::string>();
        if (filename.size() == expected.size() &&
            std::equal(filename.begin(), filename.end(), expected.begin()))
            return asset.at("size").get<std::uint64_t>();
    }
    return 0;
}
void MusicFontAssetSession::Cancel() noexcept { impl_->Cancel(); }
void MusicFontAssetSession::Shutdown() noexcept { impl_->stopped = true; impl_->Cancel(); }
}  // namespace lume::shell
