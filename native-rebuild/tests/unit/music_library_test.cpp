#include "core/music/music_library.h"
#include "core/music/music_model.h"

#include <iostream>
#include <limits>

namespace {
bool passed = true;
void Check(bool value, const char* message) {
    if (!value) { passed = false; std::cerr << "FAIL: " << message << '\n'; }
}
lume::music::LibraryEntry Entry(unsigned number) {
    lume::music::LibraryEntry entry;
    entry.track.id = "local-" + std::to_string(number);
    entry.track.source = "C:\\fixtures\\" + std::to_string(number) + ".mp3";
    entry.track.title = "Fixture";
    entry.track.duration = 120;
    entry.identity = "volume-file-" + std::to_string(number);
    entry.bytes = 200;
    entry.modified = 123;
    return entry;
}
}

int main() {
    using namespace lume::music;
    MusicLibrary library(1000);
    for (unsigned i = 0; i < 1000; ++i)
        Check(library.Upsert(Entry(i)) == LibraryChange::Added, "bounded cumulative insertion");
    Check(library.Upsert(Entry(1000)) == LibraryChange::Full, "capacity does not evict existing imports");
    Check(library.Upsert(Entry(3)) == LibraryChange::Unchanged, "repeat imports are deduplicated");
    auto moved = Entry(3);
    moved.track.id = "local-1003";
    moved.track.source = "C:\\fixtures\\moved.mp3";
    Check(library.Upsert(moved) == LibraryChange::Updated, "same file identity follows explicit reimport");
    Check(library.Find("local-3") && !library.Find("local-1003"), "move preserves original track ID");
    auto invalid = Entry(0);
    invalid.track.duration = std::numeric_limits<double>::quiet_NaN();
    Check(library.Upsert(invalid) == LibraryChange::Invalid, "nonfinite metadata rejected");
    invalid = Entry(0);
    invalid.track.id = "local-../../outside";
    Check(library.Upsert(invalid) == LibraryChange::Invalid, "path-like artwork and track IDs rejected");
    const auto encoded = library.Encode();
    MusicLibrary restored(1000);
    Check(restored.Decode(encoded) && restored.Entries().size() == 1000, "index round trip");
    Check(!restored.Decode("{\"version\":999,\"tracks\":[]}"), "unknown index version rejected");
    Check(!restored.Decode("{\"version\":1,\"version\":1,\"tracks\":[]}"), "duplicate keys rejected");
    Check(!restored.Decode(std::string(kMaxLibraryIndexBytes + 1, ' ')), "index byte limit enforced");
    Check(restored.Entries().size() == 1000, "bad index does not destroy in-memory library");
    Check(!MusicLibrary(10).Decode(encoded), "oversized index rejected without truncation");

    MusicSnapshot queue;
    queue.queue = {restored.Find("local-1")->track, restored.Find("local-3")->track};
    queue.currentIndex = 1;
    queue.current = queue.queue[1];
    queue.position = 20;
    queue.playing = true;
    queue.repeat = RepeatMode::All;
    queue.shuffle = true;
    queue.volume = 0.4;
    queue.muted = true;
    const auto saved = restored.EncodeQueue(queue);
    Check(saved && saved->find("source") == std::string::npos &&
        saved->find("C:") == std::string::npos, "queue stores IDs, never file paths");
    auto resumed = saved ? restored.DecodeQueue(*saved) : std::nullopt;
    Check(resumed && resumed->current && resumed->current->id == "local-3" &&
        resumed->position == 20 && !resumed->playing && resumed->shuffle &&
        resumed->repeat == RepeatMode::All && resumed->volume == 0.4 && resumed->muted,
        "queue restores identity and preferences without autoplay");
    restored.SetAvailability("local-1", "missing");
    resumed = restored.DecodeQueue(*saved);
    Check(resumed && resumed->queue.size() == 1 && resumed->currentIndex == 0 &&
        resumed->position == 20, "unavailable predecessor does not change current identity");
    Check(restored.Remove("local-3"), "remove by ID");
    resumed = restored.DecodeQueue(*saved);
    Check(resumed && resumed->queue.empty() && !resumed->current && resumed->position == 0,
        "removed and unavailable queue references are dropped");
    queue.position = std::numeric_limits<double>::infinity();
    Check(!restored.EncodeQueue(queue), "invalid queue timeline rejected");

    MusicLibrary filtered;
    MusicSnapshot live;
    for (unsigned id : {4u, 1u, 3u, 2u}) {
        Check(filtered.Upsert(Entry(id)) == LibraryChange::Added, "queue filtering fixture");
        live.queue.push_back(Entry(id).track);
    }
    live.currentIndex = 2;
    live.current = live.queue[2];
    live.position = 37;
    live.playing = true;
    live.volume = 0.25;
    live.muted = true;
    live.repeat = RepeatMode::One;
    live.shuffle = true;
    const auto roundTrip = [&]() {
        const auto bytes = filtered.EncodeQueue(live);
        Check(bytes.has_value(), "deleted library IDs do not block queue encoding");
        return bytes ? filtered.DecodeQueue(*bytes) : std::optional<MusicSnapshot>{};
    };
    Check(filtered.Remove("local-1"), "remove noncurrent queued item");
    resumed = roundTrip();
    Check(resumed && resumed->queue.size() == 3 &&
        resumed->queue[0].id == "local-4" && resumed->queue[1].id == "local-3" &&
        resumed->queue[2].id == "local-2" && resumed->currentIndex == 1 &&
        resumed->current && resumed->current->id == "local-3" && resumed->position == 37,
        "filtering preserves queue order, surviving current identity and progress");
    live.position = 51;
    resumed = roundTrip();
    Check(resumed && resumed->position == 51, "later progress still saves after deletion");
    Check(filtered.Remove("local-3"), "remove current library item without changing runtime queue");
    resumed = roundTrip();
    Check(resumed && resumed->queue.size() == 2 && resumed->queue[0].id == "local-4" &&
        resumed->queue[1].id == "local-2" && resumed->current &&
        resumed->current->id == "local-2" && resumed->position == 0 &&
        resumed->volume == live.volume && resumed->muted && resumed->shuffle &&
        resumed->repeat == live.repeat && !resumed->playing,
        "removed current selects next survivor at zero while preserving preferences");
    auto compact = live;
    compact.current.reset();
    const auto compactBytes = filtered.EncodeQueue(compact);
    const auto compactRestored = compactBytes ? filtered.DecodeQueue(*compactBytes) : std::nullopt;
    Check(compactRestored && compactRestored->current &&
        compactRestored->current->id == "local-2" && compactRestored->position == 0,
        "worker snapshot without current object remains supported");
    Check(filtered.Remove("local-2"), "remove last successor");
    resumed = roundTrip();
    Check(resumed && resumed->queue.size() == 1 && resumed->current &&
        resumed->current->id == "local-4" && resumed->position == 0,
        "removed current falls back to preceding survivor when no successor remains");
    Check(filtered.Remove("local-4"), "remove all remaining queued library items");
    resumed = roundTrip();
    Check(resumed && resumed->queue.empty() && !resumed->current &&
        resumed->currentIndex == 0 && resumed->position == 0 &&
        resumed->volume == live.volume && resumed->muted && resumed->shuffle &&
        resumed->repeat == live.repeat, "all deleted items encode a valid empty queue with preferences");
    Check(live.queue.size() == 4 && live.queue[1].id == "local-1" &&
        live.current && live.current->id == "local-3" && live.currentIndex == 2 &&
        live.position == 51 && live.playing, "encoding never mutates current playback or runtime queue");
    Check(filtered.Upsert(Entry(5)) == LibraryChange::Added, "add new track after deletions");
    live.queue.push_back(Entry(5).track);
    resumed = roundTrip();
    Check(resumed && resumed->queue.size() == 1 && resumed->current &&
        resumed->current->id == "local-5", "subsequent queue additions save and restore despite deleted IDs");

    auto badQueue = live;
    badQueue.queue.push_back(live.queue[0]);
    Check(!filtered.EncodeQueue(badQueue), "duplicate deleted IDs are rejected before filtering");
    badQueue = live;
    badQueue.queue.push_back(live.queue.back());
    Check(!filtered.EncodeQueue(badQueue), "duplicate surviving IDs are still rejected");
    for (const auto* id : {"", "local-", "local-../1", "netease:1"}) {
        badQueue = live;
        badQueue.queue[0].id = id;
        Check(!filtered.EncodeQueue(badQueue), "illegal and online IDs are rejected instead of filtered");
    }
    badQueue = live;
    badQueue.current->id = "local-1";
    Check(!filtered.EncodeQueue(badQueue), "mismatched deleted current is rejected before filtering");
    badQueue = live;
    badQueue.currentIndex = badQueue.queue.size();
    Check(!filtered.EncodeQueue(badQueue), "out-of-range current index rejected before filtering");
    badQueue = live;
    badQueue.position = std::numeric_limits<double>::quiet_NaN();
    Check(!filtered.EncodeQueue(badQueue), "removed current does not conceal invalid progress");
    badQueue = live;
    badQueue.volume = 1.1;
    Check(!filtered.EncodeQueue(badQueue), "invalid volume still rejected");
    badQueue = live;
    badQueue.repeat = static_cast<RepeatMode>(99);
    Check(!filtered.EncodeQueue(badQueue), "invalid repeat still rejected");
    badQueue = {};
    badQueue.currentIndex = 1;
    Check(!filtered.EncodeQueue(badQueue), "empty queue requires zero current index");
    badQueue = {};
    badQueue.current = Entry(1).track;
    Check(!filtered.EncodeQueue(badQueue), "empty queue cannot name a current track");
    badQueue.current = Track{};
    Check(!filtered.EncodeQueue(badQueue), "empty queue cannot contain an empty current object");
    badQueue = live;
    badQueue.queue.resize(kMaxMusicTracks + 1, Entry(99).track);
    Check(!filtered.EncodeQueue(badQueue), "filtering cannot bypass queue size limit");

    NativeTrackOwner owner;
    owner.Replace(library.Tracks(), true);
    Check(owner.Tracks().size() == kMaxMusicTracks && owner.IsImported(owner.Tracks().front()),
        "existing NativeTrackOwner remains compatible and bounded");
    if (!passed) return 1;
    std::cout << "music library model/index/queue checks passed\n";
}
