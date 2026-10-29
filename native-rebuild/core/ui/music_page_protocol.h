#pragma once

#include "core/music/music_types.h"
#include "core/music/music_lyrics.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lume::ui {

enum class MusicAction {
    Snapshot,
    Open,
    Close,
    Visibility,
    Login,
    Logout,
    LoadHome,
    LoadLyrics,
    LoadScore,
    Play,
    Pause,
    Toggle,
    Previous,
    Next,
    Seek,
    PlayNow,
    Enqueue,
    RemoveFromQueue,
    ClearQueue,
    SetRepeat,
    SetShuffle,
    InsertNext,
    SetVolume,
    SetMuted,
    SetQuality,
};

struct MusicPageCommand {
    MusicAction action = MusicAction::Snapshot;
    std::uint64_t session = 0;
    // PlayNow requires both a bounded nonempty id and its matching queue index.
    std::string id;
    std::string value;
    double position = 0.0;
    std::size_t index = 0;
    bool flag = false;
    double volume = 1.0;
};

struct MusicPageState {
    music::MusicSnapshot snapshot;
    bool pageOpen = false;
    bool pageVisible = false;
    bool accountSignedIn = false;
    std::string error;
    std::optional<music::AudioQualityState> audio;
};

struct MusicLyricsMessage {
    std::uint64_t session = 0;
    std::string trackId;
    std::vector<music::LyricLine> lines;
};

std::optional<MusicPageCommand> ParseMusicPageJson(std::wstring_view json) noexcept;
std::wstring EncodeMusicPageState(const MusicPageState& state);
std::wstring EncodeMusicWidgetState(const music::MusicSnapshot& snapshot);
std::wstring EncodeMusicLyrics(const MusicLyricsMessage& lyrics);
// 音符律动 melody analysis of the current track (pitch frames and beat grid).
struct MusicScoreMessage {
    std::uint64_t session = 0;
    std::string trackId;
    double hop = 0.02;
    std::vector<std::uint16_t> pitch;
    double tempo = 0;
    std::vector<double> beats;
    double start = 0, end = 0;   // audible span in seconds
};
std::wstring EncodeMusicScore(const MusicScoreMessage& score);

}  // namespace lume::ui
