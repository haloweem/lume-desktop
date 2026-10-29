#pragma once

#include "core/music/music_preferences.h"

#include <optional>

namespace lume::ui {

enum class MusicPreferencesAction { Snapshot, Apply, Reset };

struct MusicPreferencesCommand {
    MusicPreferencesAction action = MusicPreferencesAction::Snapshot;
    std::uint64_t request = 0;
    std::uint64_t revision = 0;
    std::optional<music::MusicPreferences> preferences;
};

struct MusicPreferencesMessage {
    std::uint64_t revision = 0;
    std::uint64_t request = 0;
    music::MusicPreferencesResult result = music::MusicPreferencesResult::Ready;
    music::MusicPreferences preferences;
};

std::optional<MusicPreferencesCommand> DecodeMusicPreferencesCommand(
    const nlohmann::json&, std::string* error = nullptr);
std::optional<MusicPreferencesCommand> ParseMusicPreferencesPageJson(std::wstring_view) noexcept;
nlohmann::json EncodeMusicPreferencesMessage(const MusicPreferencesMessage&);
std::wstring EncodeMusicPreferencesPageState(const MusicPreferencesMessage&);

}  // namespace lume::ui
