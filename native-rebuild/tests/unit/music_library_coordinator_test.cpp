#include "apps/shell/music_library_coordinator.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace lume;
using Coordinator = shell::MusicLibraryCoordinator;
using Result = shell::MusicLibraryWorkerResult;
using Action = shell::MusicLibraryPlaybackAction;
using Json = nlohmann::json;
void Expect(bool condition, const char* why) {
    if (!condition) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); }
}
std::wstring Wide(const Json& value) {
    const auto text = value.dump(-1, ' ', true);
    return {text.begin(), text.end()};
}
Json Page(std::string error = {}) {
    return {{"version",1},{"type","worker-state"},{"scope","music-library"},{"loaded",true},
        {"error",std::move(error)},{"total",4},{"offset",0},{"limit",100},{"tracks",Json::array({
            {{"id","a"},{"title",u8"歌曲"},{"artwork","native-placeholder"}},
            {{"id","b"},{"artwork","http://127.0.0.1:1234/lease"}},
            {{"id","c"},{"artwork","https://provider.invalid/cover"}},
            {{"id","d"},{"artwork","C:\\private\\cover.png"}}
        })}};
}
struct Fixture {
    std::vector<std::string> operations;
    std::vector<Json> messages;
    std::vector<music::MusicSnapshot> restored, saved;
    std::vector<music::Track> played, queued;
    std::string playedId;
    bool accepted = true, insertNext = false;
    std::uint64_t session = 0;
    Coordinator coordinator;

    explicit Fixture(bool available = true) : coordinator(available,
        {
            [this](bool visible) { operations.push_back(visible ? "visible" : "hidden"); },
            [this](std::size_t offset, std::size_t limit) {
                operations.push_back("page:" + std::to_string(offset) + ":" + std::to_string(limit)); return accepted;
            },
            [this](std::string id) { operations.push_back("remove:" + id); return accepted; },
            [this](std::string id, Action action) {
                operations.push_back("request:" + id + ":" + std::to_string(static_cast<int>(action))); return accepted;
            },
            [this](const music::MusicSnapshot& snapshot) { operations.push_back("save"); saved.push_back(snapshot); return accepted; }
        },
        {
            [this] { operations.push_back("session"); return session; },
            [this](const music::MusicSnapshot& snapshot) { operations.push_back("restore"); restored.push_back(snapshot); },
            [this](std::vector<music::Track> tracks, std::string id) {
                operations.push_back("play"); played = std::move(tracks); playedId = std::move(id);
            },
            [this](std::vector<music::Track> tracks, bool next) {
                operations.push_back(next ? "insert" : "enqueue"); queued = std::move(tracks); insertNext = next;
            }
        },
        [this](const std::wstring& state) { operations.push_back("post"); messages.push_back(Json::parse(state)); }) {}
};

void CommandOwnershipAndFailure() {
    Fixture f;
    Expect(f.operations.empty() && !f.coordinator.Restored(), "construction stores ports without work");
    Expect(f.coordinator.Snapshot()["tracks"].empty() && !f.coordinator.Snapshot()["loaded"].get<bool>(),
        "an uninitialized page snapshot is bounded and empty");
    ui::MusicLibraryCommand command; command.action = "visibility"; command.visible = true;
    f.coordinator.Command(command);
    command.action = "snapshot"; command.offset = 90; command.limit = 2; f.coordinator.Command(command);
    command.action = "page"; f.coordinator.Command(command);
    command.action = "remove"; command.id = "track"; f.coordinator.Command(command);
    command.action = "play"; f.coordinator.Command(command);
    command.action = "enqueue"; f.coordinator.Command(command);
    command.action = "insertNext"; f.coordinator.Command(command);
    Expect(f.operations == std::vector<std::string>{"visible","page:0:100","page:90:2","remove:track",
        "request:track:" + std::to_string(static_cast<int>(Action::Play)),
        "request:track:" + std::to_string(static_cast<int>(Action::Enqueue)),
        "request:track:" + std::to_string(static_cast<int>(Action::InsertNext))},
        "page, visibility and authorized playback requests retain their exact worker arguments");
    Expect(f.messages.empty(), "accepted asynchronous commands do not invent a success ACK");

    f.accepted = false; f.coordinator.Command(command);
    Expect(f.messages.size() == 1 && f.messages.back()["error"] == "busy" && f.messages.back()["revision"] == 1,
        "rejected worker requests publish a fresh failure ACK");
    f.coordinator.Failure("import-rejected");
    Expect(f.messages.size() == 2 && f.messages.back()["error"] == "import-rejected" &&
        f.messages.back()["revision"] == 2 && f.messages.back()["tracks"].empty(),
        "external import rejection shares the same bounded state and monotonic publication");

    Fixture unavailable(false);
    command.action = "visibility"; command.visible = false; unavailable.coordinator.Command(command);
    command.action = "page"; unavailable.coordinator.Command(command);
    Expect(unavailable.operations == std::vector<std::string>{"hidden","post"} &&
        unavailable.messages.back()["error"] == "unavailable", "visibility can retire leases even with no library root");
}

void ProjectionAndMonotonicState() {
    Fixture f; Result first; first.state = Wide(Page());
    first.playTracks.push_back(music::Track{"native", "", "", "", "PRIVATE-AUTHORIZED-SOURCE", "", 1});
    first.artworkId = "PRIVATE-ARTWORK-ID"; first.artworkUrl = "PRIVATE-ARTWORK-RESULT";
    f.coordinator.Process({first});
    const auto state = f.messages.back();
    Expect(!state.contains("scope") && state["type"] == "music-library-state" && state["revision"] == 1,
        "published state has one public type and coordinator-owned revision");
    Expect(state["tracks"][0]["artwork"] == "" && state["tracks"][1]["artwork"] == "http://127.0.0.1:1234/lease" &&
        state["tracks"][2]["artwork"] == "" && state["tracks"][3]["artwork"] == "",
        "only explicit worker media lease URLs leave the native boundary");
    Expect(state.dump().find("PRIVATE-") == std::string::npos,
        "native playback sources and artwork result fields are not forwarded as page messages");

    f.coordinator.Failure("busy");
    Expect(f.messages.back()["revision"] == 2 && f.messages.back()["total"] == 4 &&
        f.messages.back()["loaded"].get<bool>() && f.messages.back()["tracks"] == state["tracks"],
        "failure retains the last sanitized page data rather than rebuilding an empty page");
    Result stale; auto payload = Page(); payload["revision"] = 9999; stale.state = Wide(payload);
    stale.error = "worker-error";
    f.coordinator.Process({stale});
    Expect(f.messages.size() == 3 && f.messages.back()["revision"] == 3 && f.messages.back()["error"] == "",
        "one worker state is published once and cannot replace the coordinator's revision counter");
    Result failure; failure.error = "read-failed";
    f.coordinator.Process({failure});
    Expect(f.messages.size() == 4 && f.messages.back()["revision"] == 4 && f.messages.back()["error"] == "read-failed",
        "a standalone worker failure publishes through the same state owner");
    f.coordinator.Process({Result{}});
    Expect(f.messages.size() == 4, "an empty worker result does not generate a page message");
}

void RestorationAndPlaybackOrder() {
    Fixture f;
    music::MusicSnapshot snapshot; snapshot.session = 18; snapshot.position = 7.5;
    f.coordinator.SaveQueue(snapshot);
    Expect(f.saved.empty(), "persistent queue saves wait for successful library restoration");
    Result result; result.state = Wide(Page()); result.restored = true; result.restoredQueue = snapshot;
    result.action = Action::Play; result.playId = "native-track";
    result.playTracks.push_back(music::Track{"native-track", "title", "artist", "", "authorized-local-path", "", 9});
    f.coordinator.Process({result});
    Expect(f.operations == std::vector<std::string>{"post","session","restore","play"},
        "state ACK, initial queue restoration and explicit playback retain original order");
    Expect(f.coordinator.Restored() && f.restored.size() == 1 && f.restored.back().position == 7.5 &&
        f.playedId == "native-track" && f.played.front().source == "authorized-local-path",
        "authorized native tracks reach playback intact without becoming page data");
    f.coordinator.Process({result});
    Expect(f.restored.size() == 1, "a later restore marker cannot restore the persistent queue twice");
    f.coordinator.SaveQueue(snapshot);
    Expect(f.saved.size() == 1 && f.saved.front().session == 18 && f.saved.front().position == 7.5,
        "successful restoration enables the existing worker queue-save policy");

    Result enqueue; enqueue.action = Action::Enqueue; enqueue.playTracks = result.playTracks;
    f.coordinator.Process({enqueue});
    Expect(!f.insertNext && f.queued.front().source == "authorized-local-path", "enqueue retains native track identity");
    enqueue.action = Action::InsertNext; f.coordinator.Process({enqueue});
    Expect(f.insertNext, "insert-next remains distinct from enqueue");
    enqueue.error = "invalid-source"; const auto calls = f.operations.size(); f.coordinator.Process({enqueue});
    Expect(f.operations.size() == calls + 1 && f.operations.back() == "post",
        "failed result publishes its error without executing playback");

    Fixture active; active.session = 31; active.coordinator.Process({result});
    Expect(active.coordinator.Restored() && active.restored.empty() && !active.played.empty(),
        "an active playback session prevents saved-queue replacement but not explicit playback");

    Fixture retry; Result failed; failed.restored = true; failed.error = "restore-failed";
    retry.coordinator.Process({failed}); retry.coordinator.SaveQueue(snapshot);
    Expect(!retry.coordinator.Restored() && retry.saved.empty(), "restore failure cannot authorize persistent overwrite");
    failed.error.clear(); failed.restoredQueue = snapshot; retry.coordinator.Process({failed});
    Expect(retry.coordinator.Restored() && retry.restored.size() == 1, "a subsequent successful restore can open the save gate");

    Fixture absent(false); absent.coordinator.Process({failed}); absent.coordinator.SaveQueue(snapshot);
    Expect(absent.saved.empty(), "missing root still blocks saving if a synthetic restore marker arrives");
}
}

int main() {
    CommandOwnershipAndFailure(); ProjectionAndMonotonicState(); RestorationAndPlaybackOrder();
    std::cout << "PASS music library request, page projection, restoration and playback coordination\n";
}
