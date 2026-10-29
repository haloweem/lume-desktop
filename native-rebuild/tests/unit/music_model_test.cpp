#include "core/music/music_model.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <set>

using lume::music::MusicModel;
using lume::music::RepeatMode;
using lume::music::Track;

namespace {
void Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

Track Song(const char* id, double duration) {
    Track track;
    track.id = id;
    track.title = id;
    track.artist = "Lume test artist";
    track.duration = duration;
    return track;
}
}

int main() {
    MusicModel model;
    Expect(!model.Play(), "an empty queue cannot start playback");
    Expect(!model.Next(), "an empty queue cannot advance");

    model.ReplaceQueue({Song("one", 180)});
    model.Play();
    model.ReplaceQueue({Song("two", 200)});
    Expect(model.Snapshot().status == lume::music::PlaybackStatus::Ready,
        "replacement resets playing status as well as the playing flag");

    model.ReplaceQueue({Song("one", 180), Song("two", 200)});
    Expect(model.PlayNow(0), "the first track can start");
    Expect(model.Snapshot().current->id == "one", "play now selects the requested track");
    Expect(model.Snapshot().playing, "play now enters playing state");

    Expect(model.Seek(190), "seek accepts a finite position");
    Expect(model.Snapshot().position == 180, "seek clamps to the duration");
    Expect(model.Next(), "next selects the second track");
    Expect(model.Snapshot().current->id == "two", "next changes the current track");
    Expect(model.Snapshot().position == 0, "next resets the position");
    Expect(model.Snapshot().playing, "next keeps playback active");

    Expect(model.Next(), "next reaches the end of a finite queue");
    Expect(!model.Snapshot().current.has_value(), "the finite queue stops at the end");
    Expect(!model.Snapshot().playing, "end of the queue pauses playback");

    model.PlayNow(1);
    model.SetRepeat(RepeatMode::One);
    Expect(model.EndOfTrack(), "repeat-one accepts EOF");
    Expect(model.Snapshot().current->id == "two", "repeat-one keeps the current track");
    Expect(model.Snapshot().position == 0, "repeat-one resets the current position");
    Expect(model.Snapshot().playing, "repeat-one keeps playback active");
    model.Next();
    Expect(!model.Snapshot().current, "manual next is not trapped by repeat-one");

    const auto session = model.Snapshot().session;
    model.ReplaceQueue({Song("fresh", 30)});
    Expect(model.Snapshot().session != session, "replacing a queue starts a new session");
    Expect(!model.Seek(-1), "negative seek is rejected");
    Expect(model.Seek(31), "seek past the current duration is accepted");
    Expect(model.Snapshot().position == 30, "seek past the duration is clamped");

    model.ReplaceQueue({Song("local", 0)});
    Expect(model.PlayNow(0), "a local track without metadata can start");
    model.SetDuration(30);
    Expect(model.Snapshot().current->duration == 30, "host duration fills local metadata");
    Expect(model.Seek(31) && model.Snapshot().position == 30,
        "local playback seeks against the host-provided duration");

    model.ReplaceQueue({Song("a", 30), Song("b", 40), Song("c", 50)});
    model.PlayNow(1);
    model.Seek(12);
    model.Pause();
    const auto queueSession = model.Snapshot().session;
    Expect(model.RemoveFromQueue("a"), "remove before current succeeds");
    Expect(model.Snapshot().current->id == "b" && model.Snapshot().currentIndex == 0 &&
        model.Snapshot().position == 12 && !model.Snapshot().playing,
        "removing before current preserves identity, position and pause");
    Expect(model.Enqueue(Song("a", 30)), "enqueue restores a removed track");
    Expect(model.Enqueue(Song("c", 50)) && model.Snapshot().queue.back().id == "c",
        "enqueue moves an existing non-current track to the end");
    const auto revision = model.Snapshot().revision;
    Expect(!model.Enqueue(Song("b", 90)) && !model.RemoveFromQueue("missing") &&
        !model.Enqueue(Song("", 1)) && !model.Enqueue(Song("nan", std::numeric_limits<double>::quiet_NaN())) &&
        model.Snapshot().revision == revision, "invalid and current-id mutations are inert");
    Expect(model.RemoveFromQueue("b") && model.Snapshot().current->id == "a" &&
        model.Snapshot().status == lume::music::PlaybackStatus::Paused && model.Snapshot().position == 0,
        "removing paused current selects its successor without starting it");
    model.PlayNow(1);
    Expect(model.RemoveFromQueue("c") && model.Snapshot().current->id == "a" && model.Snapshot().playing,
        "removing last current selects the remaining predecessor");
    Expect(model.Snapshot().session == queueSession, "id mutations preserve authorization session");
    Expect(model.RemoveFromQueue("a") && !model.Snapshot().current && !model.Snapshot().playing &&
        model.Snapshot().status == lume::music::PlaybackStatus::Empty, "last removal becomes empty");
    Expect(!model.EndOfTrack(), "EOF on empty queue is inert");

    std::vector<Track> many;
    for (std::size_t i = 0; i < lume::music::kMaxMusicTracks + 5; ++i)
        many.push_back(Song(std::to_string(i).c_str(), 30));
    model.ReplaceQueue(many);
    Expect(model.Snapshot().queue.size() == lume::music::kMaxMusicTracks &&
        !model.Enqueue(Song("overflow", 30)), "queue capacity is bounded");
    lume::music::NativeTrackOwner owner;
    owner.Replace(many, true);
    Expect(owner.Tracks().size() == lume::music::kMaxMusicTracks && !owner.Resolve("overflow"),
        "authorization owner shares the bounded native scope");
    auto native = Song("native", 30);
    native.source = "native.wav";
    auto forged = native;
    forged.source = "forged.wav";
    owner.Replace({native, forged, Song("", 10)}, true);
    Expect(owner.Tracks().size() == 1 && owner.IsImported(native) && !owner.IsImported(forged),
        "duplicate ids keep their first native source and imported authorization is source-specific");
    const auto extra = Song("online", 30);
    Expect(owner.Authorize(extra) && owner.IsImported(native) && !owner.IsImported(extra) &&
        !owner.Authorize(forged), "appending native identities preserves but never widens import authorization");
    owner.Replace({Song("new", 1)});
    Expect(!owner.Resolve("native"), "new native scope revokes old ids");

    model.ReplaceQueue({Song("a", 30), Song("b", 40), Song("c", 50), Song("d", 60)});
    model.SetRepeat(RepeatMode::None);
    model.SetShuffle(true);
    model.PlayNow(0);
    std::set<std::string> visited;
    for (int i = 0; i < 4; ++i) {
        Expect(model.Snapshot().current && visited.insert(model.Snapshot().current->id).second,
            "shuffle visits each id once in a finite cycle");
        model.EndOfTrack();
    }
    Expect(!model.Snapshot().current && model.Play(), "play restarts an exhausted queue");
    model.SetRepeat(RepeatMode::All);
    std::string last;
    for (int i = 0; i < 20; ++i) {
        const auto id = model.Snapshot().current->id;
        Expect(last != id, "shuffle repeat-all avoids immediate repeats across cycles");
        last = id;
        model.EndOfTrack();
    }
    model.Pause();
    Expect(!model.EndOfTrack(), "paused EOF cannot advance");
    model.Previous();
    Expect(!model.Snapshot().playing, "previous preserves pause");
    model.ReplaceQueue({Song("single", 1)});
    model.Play();
    model.EndOfTrack();
    Expect(model.Snapshot().current->id == "single" && model.Snapshot().playing,
        "single-item shuffle repeat-all restarts safely");
    model.ClearQueue();
    Expect(!model.Snapshot().current && model.Snapshot().queue.empty(), "clear resets current and queue");

    model.SetShuffle(false);
    model.ReplaceQueue({Song("a", 30), Song("b", 40), Song("c", 50)});
    model.PlayNow(1);
    model.Seek(12);
    model.Pause();
    Expect(model.InsertNext(model.Snapshot().queue.front()), "insert-next accepts an aliased queue entry");
    Expect(model.Snapshot().current->id == "b" && model.Snapshot().currentIndex == 0 &&
        model.Snapshot().position == 12 && model.Snapshot().status == lume::music::PlaybackStatus::Paused &&
        model.Snapshot().queue[1].id == "a", "insert-next preserves current identity and paused timeline");
    Expect(!model.InsertNext(Song("b", 30)), "insert-next never moves the current track");
    model.SetShuffle(true);
    Expect(model.InsertNext(Song("c", 50)), "insert-next updates shuffle order too");
    model.Next();
    Expect(model.Snapshot().current->id == "c" && !model.Snapshot().playing,
        "shuffle next selects the inserted track without resuming pause");
    model.ReplaceQueue(many);
    Expect(!model.InsertNext(Song("overflow", 1)), "insert-next respects queue capacity");
    Expect(model.InsertNext(model.Snapshot().queue.back()), "a full queue still permits reordering");
    Expect(model.SetVolume(0.25), "normalized volume is accepted");
    Expect(!model.SetVolume(-1) && !model.SetVolume(1.01) &&
        !model.SetVolume(std::numeric_limits<double>::quiet_NaN()), "invalid volume cannot mutate state");
    model.SetMuted(true);
    model.ClearQueue();
    Expect(model.Snapshot().volume == 0.25 && model.Snapshot().muted, "controls survive queue replacement");
    model.InsertNext(Song("tone", 30));
    model.Play();
    lume::music::AudioAnalysis analysis;
    analysis.energy = 0.5;
    analysis.bins.fill(0.25);
    model.SetAnalysis(analysis);
    Expect(model.Snapshot().analysis.energy == 0, "muted output has no analysis");
    model.SetMuted(false);
    model.SetAnalysis(analysis);
    Expect(model.Snapshot().analysis.energy == 0.5, "decoded analysis reaches active state");
    model.Pause();
    Expect(model.Snapshot().analysis.energy == 0, "pause clears old analysis");
    model.Play();
    model.SetAnalysis(analysis);
    model.FailPlayback();
    Expect(model.Snapshot().analysis.energy == 0, "playback failure clears old analysis");

    std::cout << "music model behavior passed\n";
    return 0;
}
