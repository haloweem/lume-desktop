#pragma once

#include "core/music/music_background.h"
#include <vector>

namespace lume::ui {

inline constexpr std::size_t kMusicBackgroundMaxCommandCharacters = 1024;
inline constexpr std::size_t kMusicBackgroundMaxCatalogItems = 120;
enum class MusicBackgroundAction { Snapshot, Choose, Clear, Catalog, Select };
struct MusicBackgroundCommand {
    MusicBackgroundAction action = MusicBackgroundAction::Snapshot;
    std::uint64_t request = 0;
    std::uint64_t revision = 0;
    std::uint64_t offset = 0;
    std::uint64_t limit = 0;
    std::string id;
};
struct MusicBackgroundCatalogItem {
    std::string id;
    std::string name;
    std::string type;
    std::string thumbnailUrl;
};
struct MusicBackgroundMessage {
    std::uint64_t revision = 0;
    std::uint64_t request = 0;
    music::MusicBackgroundResult result = music::MusicBackgroundResult::Ready;
    std::optional<music::MusicBackgroundMedia> media;
    std::string mediaUrl;
};
struct MusicBackgroundCatalogMessage {
    std::uint64_t request = 0;
    music::MusicBackgroundResult result = music::MusicBackgroundResult::Ready;
    std::uint64_t total = 0;
    std::vector<MusicBackgroundCatalogItem> items;
    std::string selectedId;
};
std::optional<MusicBackgroundCommand> DecodeMusicBackgroundCommand(const nlohmann::json&);
std::optional<MusicBackgroundCommand> ParseMusicBackgroundPageJson(std::wstring_view) noexcept;
nlohmann::json EncodeMusicBackgroundMessage(const MusicBackgroundMessage&);
std::wstring EncodeMusicBackgroundPageState(const MusicBackgroundMessage&);
std::wstring EncodeMusicBackgroundCatalog(const MusicBackgroundCatalogMessage&);

}  // namespace lume::ui
