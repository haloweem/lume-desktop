#include "core/wallpaper/catalog.h"
#include "core/wallpaper/wallpaper_id_policy.h"
#include "vendor/nlohmann/json.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

namespace lume::wallpaper {
namespace {
using Json = nlohmann::json;
std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}
std::string Text(const Json& item, const char* key, std::size_t max = 4096) {
    auto found = item.find(key);
    if (found == item.end() || found->is_null()) return {};
    if (!found->is_string()) throw std::invalid_argument("field type");
    auto value = found->get<std::string>();
    if (value.size() > max || value.find('\0') != std::string::npos)
        throw std::invalid_argument("field length");
    return value;
}
std::uint64_t Number(const Json& item, const char* key, std::uint64_t max) {
    auto found = item.find(key);
    if (found == item.end() || found->is_null()) return 0;
    if (!found->is_number_integer() || (found->is_number_integer() && found->get<double>() < 0))
        throw std::invalid_argument("number type");
    const auto value = found->get<std::uint64_t>();
    if (value > max) throw std::invalid_argument("number range");
    return value;
}
std::string First(const Json& item, std::initializer_list<const char*> keys) {
    for (auto key : keys) {
        auto value = Text(item, key);
        if (!value.empty()) return value;
    }
    return {};
}
bool VideoExtension(const std::string& ext) {
    return ext == ".mp4" || ext == ".webm" || ext == ".mov" || ext == ".m4v" ||
        ext == ".mkv" || ext == ".avi";
}
} // namespace

std::string NormalizeOfficialUrl(const std::string& value) noexcept {
    try {
        if (value.empty() || value.size() > 4096 ||
            value.find_first_of("\\\r\n\t#") != std::string::npos) return {};
        for (unsigned char ch : value) if (ch < 32 || ch == 127) return {};
        const auto schemeEnd = value.find("://");
        if (schemeEnd == std::string::npos) return {};
        auto scheme = Lower(value.substr(0, schemeEnd));
        const auto authorityStart = schemeEnd + 3;
        const auto pathStart = value.find_first_of("/?", authorityStart);
        auto authority = Lower(value.substr(authorityStart, pathStart - authorityStart));
        if (authority.find('@') != std::string::npos || authority.empty()) return {};
        const auto port = authority.find(':');
        if (port != std::string::npos) {
            if (authority.substr(port) != ":443" || scheme != "https") return {};
            authority.resize(port);
        }
        if (authority != "static.lumedesktop.com" &&
            authority != "lume-12345-1444884137.cos.ap-nanjing.myqcloud.com") return {};
        if (scheme == "http" && authority == "static.lumedesktop.com") scheme = "https";
        if (scheme != "https" || pathStart == std::string::npos || value[pathStart] != '/') return {};
        const auto suffix = value.substr(pathStart);
        if (suffix.rfind("/lume/", 0) != 0) return {};
        const auto path = suffix.substr(0, suffix.find('?'));
        std::string decoded;
        decoded.reserve(path.size());
        auto hex = [](unsigned char ch) -> int {
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
            if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
            return -1;
        };
        for (std::size_t index = 0; index < path.size(); ++index) {
            if (path[index] != '%') { decoded.push_back(path[index]); continue; }
            if (index + 2 >= path.size()) return {};
            const auto high = hex(path[index + 1]), low = hex(path[index + 2]);
            if (high < 0 || low < 0) return {};
            const auto ch = static_cast<unsigned char>((high << 4) | low);
            if (ch < 32 || ch == 127 || ch == '/' || ch == '\\' || ch == '%' ||
                ch == '?' || ch == '#') return {};
            decoded.push_back(static_cast<char>(ch)); index += 2;
        }
        for (std::size_t start = 0; start <= decoded.size();) {
            const auto end = decoded.find('/', start);
            const auto segment = decoded.substr(start, end - start);
            if (segment == "." || segment == "..") return {};
            if (end == std::string::npos) break;
            start = end + 1;
        }
        return "https://static.lumedesktop.com" + suffix;
    } catch (...) { return {}; }
}

std::string MediaExtension(const CatalogItem& item) noexcept {
    try {
        const auto url = item.mediaUrl.substr(0, item.mediaUrl.find_first_of("?#"));
        const auto dot = url.find_last_of('.');
        if (dot == std::string::npos || url.find('/', dot) != std::string::npos) return {};
        auto ext = Lower(url.substr(dot));
        if (VideoExtension(ext) || ext == ".jpg" || ext == ".jpeg" || ext == ".png" ||
            ext == ".webp" || ext == ".gif" || ext == ".bmp" || ext == ".avif") return ext;
        return {};
    } catch (...) { return {}; }
}

CatalogResult ParseCatalog(const std::string& json) noexcept {
    try {
        if (json.empty() || json.size() > kMaxCatalogBytes) return {{}, "WALLPAPER_CATALOG_SIZE"};
        auto root = Json::parse(json, [](int depth, Json::parse_event_t, Json&) {
            if (depth > 24) throw std::invalid_argument("catalog depth");
            return true;
        });
        if (!root.is_object() || !root.contains("wallpapers") ||
            !root["wallpapers"].is_array() || root["wallpapers"].size() > kMaxLibraryItems)
            return {{}, "WALLPAPER_CATALOG_SCHEMA"};
        std::map<std::string, std::uint32_t> homeOrder;
        const auto covers = root.find("carouselCoverOrder");
        if (covers != root.end()) {
            if (!covers->is_array() || covers->size() > kMaxLibraryItems)
                return {{}, "WALLPAPER_CATALOG_HOME_ORDER"};
            for (std::size_t rank = 0; rank < covers->size(); ++rank) {
                const auto& value = (*covers)[rank];
                if (!value.is_string()) return {{}, "WALLPAPER_CATALOG_HOME_ORDER"};
                const auto id = value.get<std::string>();
                if (!IsSafeWallpaperId(id) ||
                    !homeOrder.emplace(Lower(id), static_cast<std::uint32_t>(rank)).second)
                    return {{}, "WALLPAPER_CATALOG_HOME_ORDER"};
            }
        }
        std::set<std::string> ids;
        CatalogResult result;
        result.items.reserve(root["wallpapers"].size());
        for (const auto& record : root["wallpapers"]) {
            if (!record.is_object()) return {{}, "WALLPAPER_CATALOG_ITEM"};
            CatalogItem item;
            item.id = Text(record, "id", 96);
            if (!IsSafeWallpaperId(item.id) || !ids.insert(Lower(item.id)).second)
                return {{}, "WALLPAPER_CATALOG_ID"};
            item.title = First(record, {"displayName", "title"});
            if (item.title.empty()) item.title = item.id;
            if (item.title.size() > 512) return {{}, "WALLPAPER_CATALOG_TITLE"};
            item.mediaUrl = NormalizeOfficialUrl(First(record,
                {"downloadUrl", "fullImageUrl", "imageUrl", "originalUrl", "sourceUrl"}));
            const auto thumbnail = First(record, {"previewThumbnailUrl", "thumbnailUrl", "heroThumbnailUrl"});
            item.thumbnailUrl = thumbnail.empty() ? std::string{} : NormalizeOfficialUrl(thumbnail);
            const auto hero = Text(record, "heroThumbnailUrl");
            item.heroThumbnailUrl = hero.empty() ? std::string{} : NormalizeOfficialUrl(hero);
            const auto preview = Text(record, "previewUrl");
            item.previewUrl = preview.empty() ? std::string{} : NormalizeOfficialUrl(preview);
            if (item.mediaUrl.empty() || (!thumbnail.empty() && item.thumbnailUrl.empty()) ||
                (!hero.empty() && item.heroThumbnailUrl.empty()) || (!preview.empty() && item.previewUrl.empty()))
                return {{}, "WALLPAPER_CATALOG_URL"};
            const auto cachedRank = record.find("homeOrder");
            if (cachedRank != record.end()) {
                if (!cachedRank->is_number_integer() || cachedRank->get<double>() < 0)
                    return {{}, "WALLPAPER_CATALOG_HOME_ORDER"};
                item.homeOrder = static_cast<std::uint32_t>(Number(record, "homeOrder", kMaxLibraryItems - 1));
            }
            // The live manifest's explicit root order is authoritative. Safe
            // IDs absent from a partial response retain their rank as a gap.
            if (covers != root.end()) {
                item.homeOrder.reset();
                const auto rank = homeOrder.find(Lower(item.id));
                if (rank != homeOrder.end()) item.homeOrder = rank->second;
            }
            const auto ext = MediaExtension(item);
            if (ext.empty()) return {{}, "WALLPAPER_CATALOG_MEDIA_TYPE"};
            item.kind = VideoExtension(ext) ? MediaKind::Video : MediaKind::Image;
            item.width = static_cast<std::uint32_t>(Number(record, "width", 32768));
            item.height = static_cast<std::uint32_t>(Number(record, "height", 32768));
            item.sizeBytes = Number(record, "sizeBytes", kMaxWallpaperBytes);
            if (!item.sizeBytes) return {{}, "WALLPAPER_CATALOG_MEDIA_SIZE"};
            item.sha256 = Lower(Text(record, "sha256", 80));
            if (item.sha256.rfind("sha256:", 0) == 0) item.sha256.erase(0, 7);
            if (item.sha256.size() != 64 || !std::all_of(item.sha256.begin(), item.sha256.end(),
                [](unsigned char ch) { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); }))
                return {{}, "WALLPAPER_CATALOG_SHA256"};
            result.items.push_back(std::move(item));
        }
        return result;
    } catch (...) { return {{}, "WALLPAPER_CATALOG_INVALID"}; }
}

void ApplyOfficialCatalogPolicy(std::vector<CatalogItem>& items) {
    items.erase(std::remove_if(items.begin(), items.end(), [](const auto& item) {
        const auto id = Lower(item.id);
        return id == "media-player" || id == "wlop-2-2" ||
            id == "hatsune-miku-night-beach" || id == "3d-snowflakes";
    }), items.end());
    for (auto& item : items) {
        const auto id = Lower(item.id);
        if (id == "forest-2") item.title = "Forest";
        else if (id == "eb-sunflower-girl") item.title = "Sunflower Girl";
    }
}

namespace {
namespace fs = std::filesystem;
std::atomic<std::uint64_t> cacheSequence{0};
Json CachedItem(const CatalogItem& item) {
    Json result = {{"id", item.id}, {"title", item.title}, {"thumbnailUrl", item.thumbnailUrl},
        {"downloadUrl", item.mediaUrl}, {"width", item.width}, {"height", item.height},
        {"sizeBytes", item.sizeBytes}, {"sha256", item.sha256}};
    if (!item.heroThumbnailUrl.empty()) result["heroThumbnailUrl"] = item.heroThumbnailUrl;
    if (!item.previewUrl.empty()) result["previewUrl"] = item.previewUrl;
    if (item.homeOrder) result["homeOrder"] = *item.homeOrder;
    return result;
}
CatalogCacheStatus CacheError(const char* error) { return {false, false, error}; }
bool WriteCache(const fs::path& path, const std::string& text) {
#ifdef _WIN32
    const auto file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
        written == text.size() && FlushFileBuffers(file);
    CloseHandle(file); return ok;
#else
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size())); file.flush();
    return static_cast<bool>(file);
#endif
}
bool ReplaceCache(const fs::path& from, const fs::path& to) {
#ifdef _WIN32
    return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::error_code error; fs::rename(from, to, error); return !error;
#endif
}
}

CatalogCache::CatalogCache(std::string root) {
    try {
        if (!root.empty() && root.find('\0') == std::string::npos)
            root_ = fs::absolute(fs::u8path(root)).lexically_normal().u8string();
    } catch (...) {}
}
CatalogCacheStatus CatalogCache::Load() noexcept {
    try {
        if (root_.empty()) return CacheError("WALLPAPER_CATALOG_CACHE_ROOT");
        const auto path = fs::u8path(root_) / "catalog-cache.json";
        if (!fs::exists(path)) return {true, false, {}};
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) return CacheError("WALLPAPER_CATALOG_CACHE_READ");
        const auto size = input.tellg();
        if (size <= 0 || static_cast<std::uint64_t>(size) > kMaxCatalogBytes)
            return CacheError("WALLPAPER_CATALOG_CACHE_SIZE");
        std::string text(static_cast<std::size_t>(size), '\0');
        input.seekg(0);
        if (!input.read(text.data(), static_cast<std::streamsize>(text.size())))
            return CacheError("WALLPAPER_CATALOG_CACHE_READ");
        auto root = Json::parse(text, [](int depth, Json::parse_event_t, Json&) {
            if (depth > 24) throw std::invalid_argument("cache depth");
            return true;
        });
        if (!root.is_object() || !root.contains("version") || !root["version"].is_number_integer() ||
            root["version"] != 1 || !root.contains("imageCatalogLoaded") ||
            !root["imageCatalogLoaded"].is_boolean() || !root.contains("videoCatalogLoaded") ||
            !root["videoCatalogLoaded"].is_boolean())
            return CacheError("WALLPAPER_CATALOG_CACHE_SCHEMA");
        const auto images = root["imageCatalogLoaded"].get<bool>();
        const auto videos = root["videoCatalogLoaded"].get<bool>();
        if (!images && !videos) return CacheError("WALLPAPER_CATALOG_CACHE_SCHEMA");
        auto parsed = ParseCatalog(text);
        if (!parsed.error.empty()) return CacheError("WALLPAPER_CATALOG_CACHE_CATALOG");
        for (const auto& item : parsed.items) {
            if ((item.kind == MediaKind::Image && !images) || (item.kind == MediaKind::Video && !videos))
                return CacheError("WALLPAPER_CATALOG_CACHE_COLLECTION");
        }
        ApplyOfficialCatalogPolicy(parsed.items);
        items_ = std::move(parsed.items);
        imageCatalogLoaded_ = images; videoCatalogLoaded_ = videos;
        return {true, true, {}};
    } catch (...) { return CacheError("WALLPAPER_CATALOG_CACHE_INVALID"); }
}
CatalogCacheStatus CatalogCache::Save(const CatalogResult& refreshed) noexcept {
    fs::path staging;
    auto cleanup = [&] { if (!staging.empty()) { std::error_code error; fs::remove(staging, error); } };
    try {
        if (root_.empty()) return CacheError("WALLPAPER_CATALOG_CACHE_ROOT");
        if (!refreshed.imageCatalogLoaded && !refreshed.videoCatalogLoaded)
            return CacheError("WALLPAPER_CATALOG_CACHE_NO_REFRESH");
        Json records = Json::array();
        for (const auto& item : items_) {
            if ((item.kind == MediaKind::Image && !refreshed.imageCatalogLoaded) ||
                (item.kind == MediaKind::Video && !refreshed.videoCatalogLoaded))
                records.push_back(CachedItem(item));
        }
        for (const auto& item : refreshed.items) {
            if ((item.kind == MediaKind::Image && !refreshed.imageCatalogLoaded) ||
                (item.kind == MediaKind::Video && !refreshed.videoCatalogLoaded))
                return CacheError("WALLPAPER_CATALOG_CACHE_COLLECTION");
            CatalogItem probe = item;
            const auto extension = MediaExtension(probe);
            if (extension.empty() || (VideoExtension(extension) != (item.kind == MediaKind::Video)))
                return CacheError("WALLPAPER_CATALOG_CACHE_COLLECTION");
            records.push_back(CachedItem(item));
        }
        if (records.size() > kMaxLibraryItems) return CacheError("WALLPAPER_CATALOG_CACHE_SIZE");
        const auto images = imageCatalogLoaded_ || refreshed.imageCatalogLoaded;
        const auto videos = videoCatalogLoaded_ || refreshed.videoCatalogLoaded;
        auto snapshot = Json{{"version", 1}, {"imageCatalogLoaded", images},
            {"videoCatalogLoaded", videos}, {"wallpapers", records}};
        auto parsed = ParseCatalog(snapshot.dump());
        if (!parsed.error.empty()) return CacheError("WALLPAPER_CATALOG_CACHE_CATALOG");
        ApplyOfficialCatalogPolicy(parsed.items);
        snapshot["wallpapers"] = Json::array();
        for (const auto& item : parsed.items) snapshot["wallpapers"].push_back(CachedItem(item));
        const auto text = snapshot.dump();
        const auto directory = fs::u8path(root_);
        fs::create_directories(directory);
        staging = directory / ("catalog-cache.json." +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
            "-" + std::to_string(++cacheSequence) + ".tmp");
        if (!WriteCache(staging, text)) { cleanup(); return CacheError("WALLPAPER_CATALOG_CACHE_WRITE"); }
        if (!ReplaceCache(staging, directory / "catalog-cache.json")) {
            cleanup(); return CacheError("WALLPAPER_CATALOG_CACHE_REPLACE");
        }
        items_ = std::move(parsed.items);
        imageCatalogLoaded_ = images; videoCatalogLoaded_ = videos;
        return {true, true, {}};
    } catch (...) { cleanup(); return CacheError("WALLPAPER_CATALOG_CACHE_SAVE"); }
}
} // namespace lume::wallpaper
