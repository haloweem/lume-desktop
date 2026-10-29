#include "core/wallpaper/library.h"
#include "core/wallpaper/wallpaper_id_policy.h"
#include "vendor/nlohmann/json.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace lume::wallpaper {
namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;
constexpr std::size_t kMaxLibraryBytes = 32u * 1024u * 1024u;
std::atomic<std::uint64_t> sequence{0};
LibraryResult Error(const char* error) { return {false, {}, {}, error}; }
std::string Unique() {
    return std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
        "-" + std::to_string(++sequence);
}
bool Read(const fs::path& path, std::string& result) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    const auto size = file.tellg();
    if (size < 0 || static_cast<std::uint64_t>(size) > kMaxLibraryBytes) return false;
    result.resize(static_cast<std::size_t>(size));
    file.seekg(0);
    return static_cast<bool>(file.read(result.data(), static_cast<std::streamsize>(result.size())));
}
bool AtomicReplace(const fs::path& from, const fs::path& to) {
#ifdef _WIN32
    return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::error_code error; fs::rename(from, to, error); return !error;
#endif
}
bool WriteFlushed(const fs::path& path, const std::string& value) {
#ifdef _WIN32
    const auto file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = value.size() <= MAXDWORD &&
        WriteFile(file, value.data(), static_cast<DWORD>(value.size()), &written, nullptr) &&
        written == value.size() && FlushFileBuffers(file);
    CloseHandle(file);
    return ok;
#else
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(value.data(), static_cast<std::streamsize>(value.size())); file.flush();
    return static_cast<bool>(file);
#endif
}
std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); }); return value;
}
bool Owned(const std::string& root, const fs::path& value, bool mustExist = true) {
    if (root.empty() || value.empty() || !value.is_absolute()) return false;
    std::error_code error;
    const auto media = fs::weakly_canonical(fs::u8path(root) / "media", error);
    if (error) return false;
    const auto resolved = fs::weakly_canonical(value, error);
    if (error || resolved.parent_path() != media) return false;
    const auto status = fs::symlink_status(value, error);
    if (status.type() == fs::file_type::not_found && !mustExist) return true;
    if (fs::is_symlink(status) || error) return false;
    return !mustExist || fs::is_regular_file(value, error);
}
bool Encrypted(const fs::path& path) {
    if (Lower(path.extension().u8string()) != ".apk") return false;
    std::ifstream input(path, std::ios::binary);
    std::array<char, 8> magic{};
    return input.read(magic.data(), magic.size()) &&
        std::string(magic.data(), magic.size()) == "LUMEWP02";
}
Json ItemJson(const CatalogItem& item) {
    Json result = {{"id", item.id}, {"title", item.title},
        {"kind", item.kind == MediaKind::Video ? "video" : "image"},
        {"thumbnailUrl", item.thumbnailUrl}, {"downloadUrl", item.mediaUrl},
        {"width", item.width}, {"height", item.height},
        {"sizeBytes", item.sizeBytes}, {"sha256", item.sha256}};
    if (!item.heroThumbnailUrl.empty()) result["heroThumbnailUrl"] = item.heroThumbnailUrl;
    if (item.homeOrder) result["homeOrder"] = *item.homeOrder;
    return result;
}
bool ManifestShape(const Json& value) {
    return value.is_object() && value.contains("version") &&
        value["version"].is_number_integer() && value["version"] == 1 &&
        value.contains("items") && value["items"].is_array() &&
        value["items"].size() <= kMaxLibraryItems;
}
} // namespace

Library::Library(std::string root) {
    try {
        if (!root.empty()) root_ = fs::absolute(fs::u8path(root)).lexically_normal().u8string();
    } catch (...) { root_.clear(); }
}

namespace {
enum class ManifestState { Missing, Valid, Unsupported, Invalid };
struct ManifestData {
    ManifestState state = ManifestState::Invalid;
    bool readable = false;
    std::string error = "WALLPAPER_LIBRARY_INVALID";
    std::string text;
    std::vector<LibraryEntry> items;
};
ManifestData ReadManifest(const std::string& root, const fs::path& path) noexcept {
    ManifestData result;
    const auto fail = [&](const char* error) {
        result.error = error;
        return std::move(result);
    };
    try {
        if (!fs::exists(path)) {
            result.state = ManifestState::Missing; result.error.clear(); return result;
        }
        if (!Read(path, result.text)) return fail("WALLPAPER_LIBRARY_READ");
        result.readable = true;
        auto json = Json::parse(result.text, [](int depth, Json::parse_event_t, Json&) {
            if (depth > 24) throw std::invalid_argument("library depth");
            return true;
        });
        if (json.is_object() && json.contains("version") &&
            (!json["version"].is_number_integer() || json["version"] != 1)) {
            result.state = ManifestState::Unsupported;
            return fail("WALLPAPER_LIBRARY_SCHEMA");
        }
        if (!ManifestShape(json)) return fail("WALLPAPER_LIBRARY_SCHEMA");
        std::vector<LibraryEntry> next;
        std::set<std::string> ids;
        std::set<std::string> paths;
        for (const auto& record : json["items"]) {
            if (!record.is_object()) return fail("WALLPAPER_LIBRARY_ITEM");
            LibraryEntry entry;
            entry.imported = record.at("imported").get<bool>();
            entry.protectedMedia = record.at("protectedMedia").get<bool>();
            if (entry.imported == entry.protectedMedia) return fail("WALLPAPER_LIBRARY_PROTECTION");
            const auto name = record.at("file").get<std::string>();
            if (name.empty() || name.size() > 220 || name.find_first_of("/\\:\0", 0, 4) != std::string::npos ||
                name == "." || name == "..") return fail("WALLPAPER_LIBRARY_PATH");
            auto mediaPath = fs::u8path(root) / "media" / fs::u8path(name);
            if (!Owned(root, mediaPath, false)) return fail("WALLPAPER_LIBRARY_PATH");
            if (!paths.insert(Lower(mediaPath.filename().u8string())).second)
                return fail("WALLPAPER_LIBRARY_PATH");
            entry.localPath = mediaPath.u8string();
            const auto& metadata = record.at("item");
            if (!entry.imported) {
                auto parsed = ParseCatalog(Json{{"wallpapers", Json::array({metadata})}}.dump());
                if (!parsed.error.empty()) return fail("WALLPAPER_LIBRARY_CATALOG");
                entry.item = std::move(parsed.items.front());
                if (mediaPath.extension() != ".apk") return fail("WALLPAPER_LIBRARY_PROTECTION");
            } else {
                entry.item.id = metadata.at("id").get<std::string>();
                entry.item.title = metadata.at("title").get<std::string>();
                const auto kind = metadata.at("kind").get<std::string>();
                if (kind != "video" && kind != "image") return fail("WALLPAPER_LIBRARY_MEDIA");
                entry.item.kind = kind == "video" ? MediaKind::Video : MediaKind::Image;
                entry.item.sizeBytes = metadata.at("sizeBytes").get<std::uint64_t>();
                entry.item.width = metadata.value("width", 0u);
                entry.item.height = metadata.value("height", 0u);
                CatalogItem probe; probe.mediaUrl = mediaPath.u8string();
                if (MediaExtension(probe).empty() || entry.item.title.empty() ||
                    entry.item.title.size() > 512 || !entry.item.sizeBytes ||
                    entry.item.sizeBytes > kMaxWallpaperBytes)
                    return fail("WALLPAPER_LIBRARY_MEDIA");
            }
            if (!IsSafeWallpaperId(entry.item.id) || !ids.insert(Lower(entry.item.id)).second)
                return fail("WALLPAPER_LIBRARY_ID");
            next.push_back(std::move(entry));
        }
        result.items = std::move(next);
        result.state = ManifestState::Valid; result.error.clear();
        return result;
    } catch (...) { return fail("WALLPAPER_LIBRARY_INVALID"); }
}
}

LibraryResult Library::Load() noexcept {
    try {
        if (root_.empty()) return Error("WALLPAPER_LIBRARY_ROOT");
        const auto directory = fs::u8path(root_);
        auto primary = ReadManifest(root_, directory / "library.json");
        if (primary.state == ManifestState::Valid) {
            items_ = std::move(primary.items); recovered_ = false;
            return {true, {}, {}, {}};
        }
        if (primary.state == ManifestState::Unsupported) return Error(primary.error.c_str());
        auto backup = ReadManifest(root_, directory / "library.json.bak");
        if (backup.state == ManifestState::Valid) {
            items_ = std::move(backup.items); recovered_ = true;
            return {true, {}, {}, {}};
        }
        if (primary.state != ManifestState::Missing) return Error(primary.error.c_str());
        if (backup.state != ManifestState::Missing) return Error("WALLPAPER_LIBRARY_BACKUP_INVALID");
        items_.clear(); recovered_ = false;
        return {true, {}, {}, {}};
    } catch (...) { return Error("WALLPAPER_LIBRARY_INVALID"); }
}

LibraryResult Library::Save() noexcept {
    fs::path staging, backupStaging;
    const auto cleanup = [&] {
        std::error_code error;
        if (!staging.empty()) fs::remove(staging, error);
        if (!backupStaging.empty()) fs::remove(backupStaging, error);
    };
    try {
        if (root_.empty() || items_.size() > kMaxLibraryItems) return Error("WALLPAPER_LIBRARY_ROOT");
        const auto directory = fs::u8path(root_);
        fs::create_directories(directory / "media");
        const auto target = directory / "library.json";
        Json records = Json::array();
        for (const auto& entry : items_) {
            const auto path = fs::u8path(entry.localPath);
            if (!Owned(root_, path, false)) return Error("WALLPAPER_LIBRARY_PATH");
            records.push_back({{"item", ItemJson(entry.item)}, {"file", path.filename().u8string()},
                {"imported", entry.imported}, {"protectedMedia", entry.protectedMedia}});
        }
        const auto text = Json{{"version", 1}, {"items", records}}.dump(2);
        if (text.size() > kMaxLibraryBytes) return Error("WALLPAPER_LIBRARY_SIZE");
        const auto primary = ReadManifest(root_, target);
        if (primary.state == ManifestState::Unsupported)
            return Error("WALLPAPER_LIBRARY_EXISTING_INVALID");
        if (primary.state != ManifestState::Valid) {
            const auto backup = ReadManifest(root_, directory / "library.json.bak");
            if (recovered_) {
                if (backup.state != ManifestState::Valid)
                    return Error("WALLPAPER_LIBRARY_BACKUP_INVALID");
                if (primary.state != ManifestState::Missing && !primary.readable)
                    return Error("WALLPAPER_LIBRARY_EXISTING_INVALID");
            } else if (primary.state != ManifestState::Missing || backup.state != ManifestState::Missing) {
                return Error("WALLPAPER_LIBRARY_EXISTING_INVALID");
            }
        }
        staging = directory / ("library.json." + Unique() + ".tmp");
        if (!WriteFlushed(staging, text)) {
            cleanup();
            return Error("WALLPAPER_LIBRARY_WRITE");
        }
        if (primary.state == ManifestState::Valid) {
            backupStaging = directory / ("library.json.bak." + Unique() + ".tmp");
            if (!WriteFlushed(backupStaging, primary.text) ||
                !AtomicReplace(backupStaging, directory / "library.json.bak")) {
                cleanup();
                return Error("WALLPAPER_LIBRARY_BACKUP");
            }
        } else if (primary.state == ManifestState::Invalid) {
            // Keep the damaged original for inspection without sacrificing the
            // validated backup used for recovery.
            const auto preserved = directory / ("library.json.corrupt." + Unique());
            if (!WriteFlushed(preserved, primary.text)) {
                std::error_code error; fs::remove(preserved, error); cleanup();
                return Error("WALLPAPER_LIBRARY_RECOVERY_PRESERVE");
            }
        }
        if (!AtomicReplace(staging, target)) {
            cleanup();
            return Error("WALLPAPER_LIBRARY_REPLACE");
        }
        recovered_ = false;
        return {true, {}, {}, {}};
    } catch (...) {
        cleanup();
        return Error("WALLPAPER_LIBRARY_SAVE");
    }
}

LibraryResult Library::ImportLocal(const std::string& value, const std::atomic<bool>* cancel) noexcept {
    fs::path copied;
    try {
        const auto cancelled = [&] { return cancel && cancel->load(); };
        if (cancelled()) return Error("WALLPAPER_CANCELLED");
        if (root_.empty() || value.empty() || value.find('\0') != std::string::npos ||
            items_.size() >= kMaxLibraryItems) return Error("WALLPAPER_IMPORT_PATH");
        const auto source = fs::u8path(value);
        if (!fs::is_regular_file(source)) return Error("WALLPAPER_IMPORT_MISSING");
        const auto size = fs::file_size(source);
        if (!size || size > kMaxWallpaperBytes) return Error("WALLPAPER_IMPORT_SIZE");
        CatalogItem item;
        item.mediaUrl = source.u8string();
        const auto ext = MediaExtension(item);
        if (ext.empty()) return Error("WALLPAPER_IMPORT_FORMAT");
        item.mediaUrl.clear();
        item.id = "custom-" + Unique();
        item.title = source.stem().u8string();
        if (item.title.empty() || item.title.size() > 512) return Error("WALLPAPER_IMPORT_TITLE");
        item.sizeBytes = size;
        item.kind = ext == ".mp4" || ext == ".webm" || ext == ".mov" || ext == ".m4v" ||
            ext == ".mkv" || ext == ".avi"
            ? MediaKind::Video : MediaKind::Image;
        const auto media = fs::u8path(root_) / "media";
        fs::create_directories(media);
        copied = media / fs::u8path(item.id + ext);
        bool copiedOk = true;
        {
            std::ifstream input(source, std::ios::binary);
            std::ofstream output(copied, std::ios::binary);
            std::array<char, 64u * 1024u> buffer{};
            std::uint64_t copiedBytes = 0;
            while (input && output && !cancelled()) {
                input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const auto count = input.gcount();
                if (!count) break;
                if (static_cast<std::uint64_t>(count) > size - copiedBytes) { copiedOk = false; break; }
                output.write(buffer.data(), count); copiedBytes += static_cast<std::uint64_t>(count);
            }
            output.flush();
            copiedOk = copiedOk && input.eof() && bool(output) && copiedBytes == size && !cancelled();
        }
        if (!copiedOk || fs::file_size(copied) != size) {
            std::error_code error; fs::remove(copied, error);
            return Error(cancelled() ? "WALLPAPER_CANCELLED" : "WALLPAPER_IMPORT_COPY");
        }
        if (cancelled()) {
            std::error_code error; fs::remove(copied, error); return Error("WALLPAPER_CANCELLED");
        }
        items_.push_back({item, copied.u8string(), true, false});
        auto saved = Save();
        if (!saved.ok) {
            items_.pop_back(); std::error_code error; fs::remove(copied, error); return saved;
        }
        return {true, item.id, copied.u8string(), {}};
    } catch (...) {
        if (!copied.empty()) { std::error_code error; fs::remove(copied, error); }
        return Error("WALLPAPER_IMPORT_FAILED");
    }
}

std::optional<LibraryEntry> Library::Find(const std::string& id) const noexcept {
    try {
        const auto found = std::find_if(items_.begin(), items_.end(),
            [&id](const auto& entry) { return entry.item.id == id; });
        return found == items_.end() ? std::optional<LibraryEntry>{} : *found;
    } catch (...) { return {}; }
}

LibraryResult Library::ResolvePlayablePath(const std::string& id) const noexcept {
    try {
        const auto entry = Find(id);
        if (!entry) return Error("WALLPAPER_NOT_FOUND");
        if (!Owned(root_, fs::u8path(entry->localPath))) return Error("WALLPAPER_FILE_MISSING");
        if (entry->protectedMedia) {
            if (!Encrypted(fs::u8path(entry->localPath))) return Error("WALLPAPER_PROTECTED_FILE_INVALID");
            if (!protectedResolver_) return Error("WALLPAPER_PROTECTION_UNAVAILABLE");
            return protectedResolver_(*entry);
        }
        return {true, id, entry->localPath, {}};
    } catch (...) { return Error("WALLPAPER_RESOLVE_FAILED"); }
}

LibraryResult Library::RegisterDownload(const CatalogItem& item, const std::string& value) noexcept {
    fs::path path;
    bool cleanupPath = false;
    const auto cleanup = [&] {
        if (cleanupPath) {
            std::error_code error;
            fs::remove(path, error);
        }
    };
    try {
        path = fs::u8path(value);
        if (!Owned(root_, path) || !Encrypted(path)) return Error("WALLPAPER_PROTECTED_FILE_INVALID");
        const auto normalizedPath = Lower(path.lexically_normal().u8string());
        const auto alreadyTracked = std::any_of(items_.begin(), items_.end(),
            [&](const auto& entry) {
                return Lower(fs::u8path(entry.localPath).lexically_normal().u8string()) == normalizedPath;
            });
        cleanupPath = !alreadyTracked;
        auto parsed = ParseCatalog(Json{{"wallpapers", Json::array({ItemJson(item)})}}.dump());
        if (!parsed.error.empty()) { cleanup(); return Error("WALLPAPER_LIBRARY_CATALOG"); }
        const auto identity = Lower(item.id);
        const auto filename = Lower(path.filename().u8string());
        for (const auto& entry : items_) {
            if (Lower(entry.item.id) == identity && (entry.item.id != item.id || entry.imported))
                { cleanup(); return Error("WALLPAPER_LIBRARY_ID"); }
            if (entry.item.id != item.id && Lower(fs::u8path(entry.localPath).filename().u8string()) == filename)
                { cleanup(); return Error("WALLPAPER_LIBRARY_PATH"); }
        }
        auto previous = items_;
        std::vector<fs::path> replacedPaths;
        for (const auto& entry : previous) {
            if (entry.item.id == item.id &&
                Lower(fs::u8path(entry.localPath).lexically_normal().u8string()) != normalizedPath)
                replacedPaths.push_back(fs::u8path(entry.localPath));
        }
        items_.erase(std::remove_if(items_.begin(), items_.end(),
            [&item](const auto& entry) { return entry.item.id == item.id; }), items_.end());
        items_.push_back({item, path.u8string(), false, true});
        auto saved = Save();
        if (!saved.ok) { items_ = std::move(previous); cleanup(); return saved; }
        for (const auto& replaced : replacedPaths) {
            std::error_code removeError;
            if (fs::is_regular_file(replaced, removeError) && !fs::remove(replaced, removeError)) {
                items_ = std::move(previous);
                const auto rollback = Save();
                cleanup();
                return Error(rollback.ok ? "WALLPAPER_FILE_BUSY" : "WALLPAPER_LIBRARY_ROLLBACK");
            }
        }
        cleanupPath = false;
        return {true, item.id, path.u8string(), {}};
    } catch (...) { cleanup(); return Error("WALLPAPER_LIBRARY_REGISTER"); }
}

LibraryResult Library::Remove(const std::string& id) noexcept {
    try {
        const auto entry = Find(id);
        if (!entry) return Error("WALLPAPER_NOT_FOUND");
        if (!Owned(root_, fs::u8path(entry->localPath), false)) return Error("WALLPAPER_LIBRARY_PATH");
        auto previous = items_;
        items_.erase(std::remove_if(items_.begin(), items_.end(),
            [&id](const auto& row) { return row.item.id == id; }), items_.end());
        auto saved = Save();
        if (!saved.ok) { items_ = std::move(previous); return saved; }
        std::error_code error;
        fs::remove(fs::u8path(entry->localPath), error);
        if (error) {
            items_ = std::move(previous);
            const auto rollback = Save();
            return Error(rollback.ok ? "WALLPAPER_FILE_BUSY" : "WALLPAPER_LIBRARY_ROLLBACK");
        }
        return {true, id, {}, {}};
    } catch (...) { return Error("WALLPAPER_LIBRARY_REMOVE"); }
}

void Library::SetProtectedResolver(ProtectedResolver resolver) noexcept {
    try { protectedResolver_ = std::move(resolver); } catch (...) {}
}
} // namespace lume::wallpaper
