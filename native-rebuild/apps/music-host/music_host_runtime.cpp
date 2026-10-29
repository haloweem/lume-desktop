#include "apps/music-host/music_host_runtime.h"

#include <utility>

namespace lume::music {

std::string MusicHostRuntime::Event(std::uint64_t sequence, const char* status, const char* error, bool analysis) const {
    HostEvent event;
    event.session = session_;
    event.sequence = sequence;
    event.status = status;
    // This legacy field is a source hint, not queue identity. Responses are
    // correlated by session/sequence, including sources beyond its wire limit.
    event.trackId = source_.size() <= 128 ? source_ : "";
    event.position = audio_.Position();
    event.duration = audio_.Duration();
    event.playing = audio_.Playing();
    event.ready = audio_.Ready();
    event.canToggle = loaded_;
    event.canSeek = loaded_ && event.duration > 0.0;
    event.error = error;
    event.volume = volume_;
    event.muted = muted_;
    if (analysis && event.playing && error[0] == '\0') event.analysis = audio_.Analysis();
    return EncodeHostEvent(event);
}

std::string MusicHostRuntime::Error(std::uint64_t sequence, const char* error) const {
    return Event(sequence, "error", error);
}

std::optional<std::string> MusicHostRuntime::Handle(std::string_view frame) {
    if (closed_) return std::nullopt;
    const auto command = DecodeHostCommand(frame);
    if (!command) return std::nullopt;

    if (session_.empty()) session_ = command->session;
    if (command->session != session_) return Error(command->sequence, "session");
    if (command->sequence <= lastSequence_) return Error(command->sequence, "sequence");
    lastSequence_ = command->sequence;

    if (command->action == "hello") return Event(command->sequence, loaded_ ? "ready" : "empty", "", command->analysis);
    if (command->action == "setVolume" || command->action == "setMuted") {
        const bool volume = command->action == "setVolume";
        if (!(volume ? audio_.SetVolume(command->value) : audio_.SetMuted(command->flag)))
            return Error(command->sequence, "control-failed");
        if (volume) volume_ = command->value;
        else muted_ = command->flag;
        return Event(command->sequence, loaded_ ? (audio_.Playing() ? "playing" : "paused") : "empty");
    }
    if (command->action == "load") {
        audio_.Stop();
        if (!audio_.Load(command->source)) {
            audio_.Stop();
            loaded_ = false;
            source_.clear();
            return Error(command->sequence, "load-failed");
        }
        source_ = command->source;
        loaded_ = true;
        return Event(command->sequence, "ready");
    }
    if (command->action == "close") {
        audio_.Stop();
        loaded_ = false;
        source_.clear();
        closed_ = true;
        return Event(command->sequence, "closed");
    }
    if (!loaded_) return Error(command->sequence, "not-loaded");

    bool accepted = false;
    if (command->action == "play") accepted = audio_.Play();
    else if (command->action == "pause") accepted = audio_.Pause();
    else if (command->action == "toggle") accepted = audio_.Playing() ? audio_.Pause() : audio_.Play();
    else if (command->action == "prepare") accepted = audio_.Prepare(command->value, command->follow);
    else if (command->action == "seek") {
        const bool playing = audio_.Playing();
        accepted = audio_.Seek(command->value);
        if (accepted && playing) accepted = audio_.Play();
    }
    else if (command->action == "next" || command->action == "previous") return Error(command->sequence, "queue-owned-by-shell");
    if (!accepted) return Error(command->sequence, command->action == "seek" ? "seek-failed" : "playback-failed");
    const char* status = audio_.Playing() ? "playing" : "paused";
    return Event(command->sequence, status);
}

}  // namespace lume::music
