#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace lume::ui {
struct MusicLibraryCommand {
    std::string action;
    std::string id;
    std::size_t offset = 0;
    std::size_t limit = 100;
    bool visible = false;
};

std::optional<MusicLibraryCommand> ParseMusicLibraryCommand(std::wstring_view) noexcept;
} // namespace lume::ui
