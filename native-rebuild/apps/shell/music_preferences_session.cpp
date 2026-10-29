#include "apps/shell/music_preferences_session.h"

#include <utility>

namespace lume::shell {

MusicPreferencesSession::MusicPreferencesSession(std::filesystem::path path, settings::IFileStore& files)
    : store_(std::move(path), files), result_(store_.Load()) {}

std::wstring MusicPreferencesSession::Message(std::uint64_t request) const {
    return ui::EncodeMusicPreferencesPageState({store_.revision(), request, result_, store_.preferences()});
}

std::wstring MusicPreferencesSession::State() const { return Message(0); }

std::wstring MusicPreferencesSession::HandlePage(const ui::MusicPreferencesCommand& command) {
    using Action = ui::MusicPreferencesAction;
    using Result = music::MusicPreferencesResult;
    const auto safe = music::kMusicPreferencesMaxSafeInteger;
    if (command.action == Action::Snapshot) {
        if (command.request != 0 || command.revision != 0 || command.preferences) result_ = Result::Invalid;
        return State();
    }
    const auto request = command.request <= safe ? command.request : 0;
    if (request == 0 || command.revision > safe ||
        (command.action != Action::Apply && command.action != Action::Reset) ||
        (command.action == Action::Apply && !command.preferences) ||
        (command.action == Action::Reset && command.preferences)) {
        result_ = Result::Invalid;
        return Message(request);
    }
    result_ = command.action == Action::Reset ? store_.Reset(command.revision) :
        store_.Save(*command.preferences, command.revision);
    return Message(request);
}
}  // namespace lume::shell
