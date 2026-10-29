#pragma once
#include "core/settings/file_store.h"
#include "core/widget/widget_preferences.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lume::notes {
using Millis = std::int64_t;
struct Note {
    std::string id, title, body;
    Millis createdAt = 0, updatedAt = 0;
};
// Notes are ordered most recently edited first.
struct NotesState {
    std::vector<Note> notes;
    // The note shown on the desktop widget; empty shows the newest note.
    std::string pinnedId;
    struct DesktopNote { std::string id; widget::Preferences preferences; };
    std::vector<DesktopNote> desktopNotes;
    bool desktopLayoutKnown = false;
};
inline constexpr widget::Limits DesktopLimits{280, 260, 220, 160, 720, 720};
constexpr std::size_t MaxNotes = 500;
constexpr std::size_t MaxTitleBytes = 600;
constexpr std::size_t MaxBodyBytes = 1000000;
constexpr std::size_t MaxFileBytes = 64u * 1024u * 1024u;
enum class LoadStatus { Fresh, Loaded, Failed };

bool ValidUtf8(const std::string &text) noexcept;
bool ValidId(const std::string &id) noexcept;
bool ValidNote(const Note &note) noexcept;
// Non-whitespace code points, the reference editor's word count for CJK text.
std::size_t CharacterCount(const std::string &text) noexcept;
// Whitespace collapsed to single spaces, at most maxCodePoints, never splitting a code point.
std::string Preview(const std::string &text, std::size_t maxCodePoints);
std::string Encode(const NotesState &state);
bool Decode(const std::string &text, NotesState &out);
const Note *Find(const NotesState &state, const std::string &id) noexcept;
// The pinned note, else the most recently edited one.
const Note *Shown(const NotesState &state) noexcept;
void SortByRecent(NotesState &state);

class NotesStore final {
  public:
    NotesStore(settings::IFileStore &files, std::string path) : files_(files), path_(std::move(path)) {}
    LoadStatus Load(NotesState &out) const;
    // Writes a temporary file and replaces the target, so a failure keeps the previous file.
    bool Save(const NotesState &state);
    const std::string &Path() const noexcept { return path_; }

  private:
    settings::IFileStore &files_;
    std::string path_;
};
} // namespace lume::notes
