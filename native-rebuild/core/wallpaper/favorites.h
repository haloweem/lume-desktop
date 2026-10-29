#pragma once

#include "core/settings/file_store.h"

#include <string>
#include <vector>

namespace lume::wallpaper {

struct LoadedFavorites {
    std::vector<std::string> ids;
    std::string status = "fresh";
    bool writable = true;
};

// Local-only IDs in a separate version-1 favorites.json document. The owner
// serializes access to a path. No player, page protocol or account dependency.
class FavoritesStore final {
public:
    explicit FavoritesStore(settings::IFileStore& files) : files_(files) {}
    LoadedFavorites Load(const std::string& path) const;
    bool Save(const std::string& path, const std::vector<std::string>& ids);

private:
    settings::IFileStore& files_;
};

} // namespace lume::wallpaper
