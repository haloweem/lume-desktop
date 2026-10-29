#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lume::wallpaper {

enum class MediaKind { Image, Video };

struct CatalogItem {
    std::string id;
    std::string title;
    MediaKind kind = MediaKind::Image;
    std::string thumbnailUrl;
    std::string mediaUrl;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t sizeBytes = 0;
    std::string sha256;
    // Card thumbnails stay lightweight; home/detail covers use this separate
    // optional source. Old manifests and version-one caches omit both fields.
    std::string heroThumbnailUrl = {};
    std::optional<std::uint32_t> homeOrder = {};
    // Optional short low-bitrate clip for previewing a video that is not
    // downloaded yet; the full file is only fetched when the user downloads it.
    std::string previewUrl = {};
};

struct CatalogResult {
    std::vector<CatalogItem> items;
    std::string error;
    // FetchCatalog marks a collection only after its whole response validates.
    // A successful empty collection is distinct from a failed refresh.
    bool imageCatalogLoaded = false;
    bool videoCatalogLoaded = false;
};

inline constexpr std::uint64_t kMaxWallpaperBytes = 1024ull * 1024ull * 1024ull;
inline constexpr std::size_t kMaxCatalogBytes = 32u * 1024u * 1024u;
inline constexpr std::size_t kMaxLibraryItems = 20000u;
// Every wallpaper request goes through the CDN; the client never reads the
// COS bucket directly, so COS only serves CDN origin pulls.
inline constexpr const char* kOfficialCatalogUrl =
    "https://static.lumedesktop.com/lume/wallpapers/official/manifest.json";
inline constexpr const char* kStaticCatalogUrl =
    "https://static.lumedesktop.com/lume/wallpapers/static/manifest.json";

// Empty means rejected. Only the original CDN's legacy HTTP is upgraded, and
// COS bucket URLs are rewritten to the CDN host.
std::string NormalizeOfficialUrl(const std::string& value) noexcept;
std::string MediaExtension(const CatalogItem& item) noexcept;
CatalogResult ParseCatalog(const std::string& json) noexcept;
// Apply only to fully validated official listings, never to the local library.
void ApplyOfficialCatalogPolicy(std::vector<CatalogItem>& items);

struct CatalogCacheStatus {
    bool ok = false;
    bool found = false;
    std::string error;
};

// Serialized by the owning wallpaper actor. Contains only official metadata.
// Save replaces successful collections independently and preserves failed ones.
// Load/Save leave the current snapshot intact on invalid data or an I/O error.
class CatalogCache final {
public:
    explicit CatalogCache(std::string root);
    CatalogCacheStatus Load() noexcept;
    CatalogCacheStatus Save(const CatalogResult& refreshed) noexcept;
    const std::vector<CatalogItem>& Items() const noexcept { return items_; }
    bool HasImageCatalog() const noexcept { return imageCatalogLoaded_; }
    bool HasVideoCatalog() const noexcept { return videoCatalogLoaded_; }
private:
    std::string root_;
    std::vector<CatalogItem> items_;
    bool imageCatalogLoaded_ = false;
    bool videoCatalogLoaded_ = false;
};

} // namespace lume::wallpaper
