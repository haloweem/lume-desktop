#include "apps/shell/music_online_session.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <thread>

using namespace lume::music::auth;
using namespace lume::shell;
using namespace std::chrono_literals;
namespace {
void Require(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
template<class F> bool Await(F predicate) {
    const auto end = std::chrono::steady_clock::now() + 3s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= end) return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}
Json Decode(const std::wstring& text) { return Json::parse(text); }
Json Complete(MusicOnlineSession& session) {
    Json result;
    Require(Await([&] {
        if (const auto message = session.DrainNotification()) {
            result = Decode(*message);
            return result["status"] != "loading";
        }
        return false;
    }), "request completes");
    return result;
}
MusicOnlineCommand Command(std::string action, std::uint64_t request, Json arguments = Json::object(),
                           Provider provider = Provider::Netease) {
    return {std::move(action), provider, request, std::move(arguments)};
}
Json Page(std::string id = "netease:42") {
    return {{"tracks", Json::array({{{"id", id}, {"title", "Fixture"},
        {"vipRequired", true}, {"membershipBadge", "netease-vip"},
        {"source", "https://fixture.invalid/private?token=synthetic-secret"},
        {"cookie", "synthetic-secret"}, {"credentials", {{"token", "synthetic-secret"}}}}})},
        {"playlists", Json::array({{{"id", "9"}, {"title", "List"}, {"owned", true},
            {"token", "synthetic-secret"}}})},
        {"credentials", {{"token", "synthetic-secret"}}}, {"url", "synthetic-secret"}};
}
struct Fixture {
    std::atomic<std::uint64_t> generation{1};
    std::atomic<bool> signedIn{true};
    std::atomic<int> calls{0}, plays{0};
    std::function<Json(std::string_view, const Json&, const Cancelled&)> answer;
    std::vector<lume::music::Track> played;
    MusicOnlineOptions Options() {
        MusicOnlineOptions options;
        options.verification = true;
        options.acquire = [&](Provider provider) -> std::optional<MusicProviderCredentialLease> {
            if (!signedIn) return {};
            const auto epoch = generation.load();
            return MusicProviderCredentialLease{provider, epoch, std::to_string(epoch),
                {{"token", "synthetic-secret"}}};
        };
        options.current = [&](const MusicProviderCredentialLease& lease) {
            return signedIn && lease.generation == generation && lease.userId == std::to_string(generation.load());
        };
        options.execute = [&](Provider, std::string_view action, const Json& args,
                              const Json& credentials, const Cancelled& stop) {
            ++calls;
            Require(credentials.at("token") == "synthetic-secret", "native execute receives fixture credentials");
            Require(args.contains("_userId"), "native execute injects leased user ID");
            if (answer) return answer(action, args, stop);
            if (action == "resolve") return Json{{"url", "https://fixture.invalid/audio?token=synthetic-secret"}};
            if (action == "lyrics") return Json{{"lrc", {{"lyric", "[00:01.00]fixture"}}}};
            return Page();
        };
        options.play = [&](std::vector<lume::music::Track> tracks, std::string, std::string) {
            played = std::move(tracks); ++plays;
        };
        return options;
    }
};
lume::music::Track Known(MusicOnlineSession& session) {
    session.HandlePage(Command("search", 1, {{"query", "fixture"}}));
    Require(Complete(session)["status"] == "ready", "fixture page ready");
    lume::music::Track track;
    track.id = "netease:42"; track.source = "online:" + track.id; track.duration = 10;
    return track;
}
template<class F> void Reject(F action, const char* message) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    Require(rejected, message);
}
void BusyWrites() {
    for (const auto* write : {"like", "createPlaylist", "addToPlaylist"}) {
        Fixture fixture;
        MusicOnlineSession session(fixture.Options());
        Known(session);
        std::atomic<bool> entered{false}, release{false}, cancelled{false};
        fixture.answer = [&](std::string_view, const Json&, const Cancelled& stop) {
            entered = true;
            Require(Await([&] { return release.load() || stop(); }), "write released");
            cancelled = stop();
            return Json{{"liked", true}};
        };
        session.HandlePage(Command(write, 2, {{"id", "netease:42"}, {"value", true}}));
        Require(Await([&] { return entered.load(); }), "write entered");
        std::uint64_t request = 3;
        for (const auto* action : {"like", "home", "likes", "play", "enqueue", "insertNext"}) {
            const auto reply = Decode(session.HandlePage(Command(action, request, {{"id", "netease:42"}})));
            Require(reply["request"] == request && reply["action"] == action &&
                reply["error"] == "busy" && reply["status"] == "error", "busy acknowledges incoming request");
            ++request;
        }
        release = true;
        const auto result = Complete(session);
        Require(result["request"] == 2 && result["action"] == write && result["status"] == "ready",
            "busy replies do not relabel active write result");
        Require(!cancelled && fixture.calls == 2 && fixture.plays == 0, "play cannot cancel or duplicate a write");
        session.HandlePage(Command("like", request - 1, {{"id", "netease:42"}}));
        session.Shutdown();
        Require(fixture.calls == 2, "replay of rejected request cannot become a later write");
    }
}
void SupersededReadAndInvalidPlay() {
    for (const bool invalidPlay : {false, true}) {
        Fixture fixture;
        std::atomic<bool> entered{false}, cancelled{false};
        int reads = 0;
        fixture.answer = [&](std::string_view, const Json&, const Cancelled& stop) {
            if (++reads == 1) {
                entered = true;
                Require(Await(stop), "superseded read observes cancellation");
                cancelled = true;
                return Page("netease:obsolete");
            }
            return Page();
        };
        MusicOnlineSession session(fixture.Options());
        session.HandlePage(Command("home", 1));
        Require(Await([&] { return entered.load(); }), "old read entered");
        const auto reply = Decode(session.HandlePage(Command(invalidPlay ? "play" : "search", 2,
            invalidPlay ? Json{{"id", "netease:unknown"}} : Json{{"query", "new"}})));
        if (invalidPlay) {
            Require(reply["error"] == "not-found", "unknown playback ID rejected");
            Require(Await([&] { return cancelled.load(); }), "invalid play still fences previous read");
            Require(Await([&] { return !session.HasWork(); }), "invalid play leaves no relabeled notification");
        } else {
            const auto result = Complete(session);
            Require(result["request"] == 2 && result["data"]["tracks"][0]["id"] == "netease:42",
                "only newest read publishes");
        }
        session.Shutdown();
    }
}
void ProjectionAndRegistry() {
    Fixture fixture;
    MusicOnlineSession session(fixture.Options());
    session.HandlePage(Command("home", 1));
    const auto result = Complete(session);
    Require(result.dump().find("synthetic-secret") == std::string::npos, "secrets and signed URLs excluded from page");
    Require(result["data"]["tracks"][0].size() == 9 &&
        result["data"]["tracks"][0]["source"] == "online:netease:42" &&
        result["data"]["tracks"][0]["vipRequired"] == true &&
        result["data"]["tracks"][0]["membershipBadge"] == "netease-vip" &&
        result["data"]["playlists"][0].size() == 7, "public fields use fixed projection");
    auto forged = lume::music::Track{};
    forged.id = "netease:999"; forged.source = "online:" + forged.id;
    Reject([&] { session.ResolveSource(forged, {}); }, "unregistered resolution cannot acquire network access");
    Require(!session.LoadLyrics(forged, {}).error.empty(), "unregistered lyrics rejected");
    Require(fixture.calls == 1, "forged tracks never reach execute");
    const auto played = Decode(session.HandlePage(Command("play", 2, {{"id", "netease:42"}})));
    Require(played["status"] == "ready" && fixture.played.size() == 1, "known track can play");
    const auto track = fixture.played.front();
    Require(session.ResolveSource(track, {}).find("synthetic-secret") != std::string::npos,
        "signed URL remains available to native playback");
    Require(session.LoadLyrics(track, {}).error.empty(), "known track lyrics parsed");
    session.Cancel();
    Require(!session.DrainNotification(), "page close removes pending notifications");
    Require(!session.ResolveSource(track, {}).empty(), "page close preserves authorized background queue");
    ++fixture.generation;
    Reject([&] { session.ResolveSource(track, {}); }, "old queue cannot borrow a new account lease");
    session.AccountsChanged();
    Require(Decode(session.HandlePage(Command("play", 3, {{"id", track.id}})))["error"] == "not-found",
        "account change removes stale registry entry");
    session.Shutdown();
    Reject([&] { session.ResolveSource(track, {}); }, "shutdown rejects new resolution");
}
void AccountChangeDuringRead(std::string action = "home") {
    Fixture fixture;
    std::atomic<bool> entered{false}, release{false}, sawCancel{false};
    fixture.answer = [&](std::string_view, const Json&, const Cancelled& stop) {
        entered = true;
        Require(Await([&] { return release.load(); }), "late response released");
        sawCancel = stop();
        return Page();
    };
    MusicOnlineSession session(fixture.Options());
    session.HandlePage(Command(std::move(action), 1));
    Require(Await([&] { return entered.load(); }), "revoked read entered");
    ++fixture.generation;
    session.AccountsChanged();
    const auto revoked = Complete(session);
    Require(revoked["request"] == 1 && revoked["error"] == "signed-out" &&
        revoked["data"]["tracks"].empty(), "revocation settles pending request before transport returns");
    release = true;
    Require(Await([&] { return !session.HasWork(); }), "late revoked read retired");
    Require(sawCancel && !session.DrainNotification(), "revoked response cannot repopulate page");
}
void AccountChangeDuringAcquire() {
    Fixture fixture;
    std::atomic<bool> entered{false}, release{false};
    auto options = fixture.Options();
    const auto acquire = options.acquire;
    options.acquire = [&](Provider provider) {
        entered = true;
        Require(Await([&] { return release.load(); }), "lease read released");
        return acquire(provider);
    };
    MusicOnlineSession session(options);
    session.HandlePage(Command("createPlaylist", 1, {{"title", "Fixture"}}));
    Require(Await([&] { return entered.load(); }), "lease read entered");
    ++fixture.generation;
    session.AccountsChanged();
    release = true;
    const auto result = Complete(session);
    Require(result["error"] == "signed-out" && fixture.calls == 0,
        "command waiting for credentials cannot execute under replacement account");
    session.Shutdown();
}
void ShutdownResolution() {
    Fixture fixture;
    MusicOnlineSession session(fixture.Options());
    const auto track = Known(session);
    std::atomic<bool> entered{false};
    fixture.answer = [&](std::string_view, const Json&, const Cancelled& stop) {
        entered = true;
        Require(Await(stop), "shutdown revokes source and lyric operations");
        return Json{{"url", "https://fixture.invalid/late"}, {"lrc", {{"lyric", "[00:01]late"}}}};
    };
    auto resolution = std::async(std::launch::async, [&] {
        Reject([&] { session.ResolveSource(track, {}); }, "shutdown drops late source");
    });
    Require(Await([&] { return entered.load(); }), "resolution entered");
    entered = false;
    auto lyrics = std::async(std::launch::async, [&] {
        Require(!session.LoadLyrics(track, {}).error.empty(), "shutdown drops late lyrics");
    });
    Require(Await([&] { return entered.load(); }), "lyrics entered");
    session.Shutdown();
    resolution.get();
    lyrics.get();
    Require(!session.HasWork() && !session.DrainNotification(), "shutdown leaves no work");
    Require(!session.LoadLyrics(track, {}).error.empty(), "closed session rejects lyrics");
}
void CancellationAndErrors() {
    for (const bool shutdown : {false, true}) {
        Fixture fixture;
        std::atomic<bool> entered{false}, cancelled{false};
        fixture.answer = [&](std::string_view, const Json&, const Cancelled& stop) {
            entered = true;
            Require(Await(stop), "cancel stops page operation");
            cancelled = true;
            return Page();
        };
        MusicOnlineSession session(fixture.Options());
        session.HandlePage(Command("createPlaylist", 1, {{"title", "Fixture"}}));
        Require(Await([&] { return entered.load(); }), "cancel fixture entered");
        if (shutdown) session.Shutdown();
        else {
            Require(Decode(session.HandlePage(Command("cancel", 2)))["status"] == "idle", "explicit cancellation accepted");
            Require(Await([&] { return !session.HasWork(); }), "cancelled work retired");
        }
        Require(cancelled && !session.DrainNotification(), "late write result discarded");
    }
    Fixture fixture;
    fixture.answer = [](std::string_view, const Json&, const Cancelled&) -> Json {
        throw std::runtime_error("synthetic-secret");
    };
    MusicOnlineSession session(fixture.Options());
    session.HandlePage(Command("home", 1));
    const auto result = Complete(session);
    Require(result["error"] == "service" && result.dump().find("synthetic-secret") == std::string::npos,
        "raw exception redacted");
}
void Parser() {
    Require(ParseMusicOnlineCommand(LR"({"version":1,"scope":"music-online","action":"search","provider":"netease","request":1,"query":"fixture"})").has_value(),
        "valid parser fixture");
    for (const auto* input : {
        LR"({"version":1,"scope":"music-online","action":"likes","provider":"netease","request":1})",
        LR"({"version":1,"scope":"music-online","action":"likes","provider":"qq","request":2})",
        LR"({"version":1,"scope":"music-online","action":"likes","provider":"kugou","request":3})"}) {
        const auto command = ParseMusicOnlineCommand(input);
        Require(command && command->action == "likes" && command->arguments.empty(),
            "likes accepts only provider and request beyond the protocol envelope");
    }
    for (const auto* input : {
        LR"({"version":1,"scope":"music-online","action":"home","provider":"netease","request":0})",
        LR"({"version":1,"scope":"music-online","action":"home","provider":"netease","request":9007199254740992})",
        LR"({"version":1,"scope":"music-online","action":"home","provider":"netease","request":1,"request":2})",
        LR"({"version":1,"scope":"music-online","action":"home","provider":"netease","request":1,"credentials":{}})",
        LR"({"version":1,"scope":"music-online","action":"home","provider":"netease","request":1,"_userId":"forged"})",
        LR"({"version":1,"scope":"music-online","action":"play","provider":"netease","request":1,"id":"https://fixture.invalid"})",
        LR"({"version":1,"scope":"music-online","action":"likes","provider":"qq","request":1,"offset":0})",
        LR"({"version":1,"scope":"music-online","action":"likes","provider":"qq","request":1,"limit":50})",
        LR"({"version":1,"scope":"music-online","action":"likes","provider":"qq","request":1,"id":"qq:42"})",
        LR"({"version":1,"scope":"music-online","action":"likes","provider":"qq","request":1,"query":"fixture"})",
        LR"({"version":1,"scope":"music-online","action":"likes","provider":"qq","request":1,"_userId":"forged"})"})
        Require(!ParseMusicOnlineCommand(input), "hostile parser fixture rejected");
}
void PublicDataValidation() {
    Fixture fixture;
    auto page = Page();
    page["tracks"].push_back(page["tracks"][0]);
    page["tracks"].push_back({{"id", "netease:43"}, {"title", "Second"}});
    fixture.answer = [&](std::string_view, const Json&, const Cancelled&) { return page; };
    MusicOnlineSession session(fixture.Options());
    session.HandlePage(Command("home", 1));
    Require(Complete(session)["data"]["tracks"].size() == 2, "duplicate tracks are deduplicated");
    session.HandlePage(Command("play", 2, {{"id", "netease:43"}}));
    Require(fixture.played.size() == 2 && fixture.played[0].id == "netease:42" &&
        fixture.played[1].id == "netease:43", "play preserves displayed list order");
    session.HandlePage(Command("insertNext", 3, {{"id", "netease:43"}}));
    Require(fixture.played.size() == 1 && fixture.played[0].id == "netease:43",
        "insert-next selects only requested track");
    std::uint64_t request = 4;
    for (const auto& invalid : {
            Json{{"id", "qq:42"}, {"title", "Foreign"}},
            Json{{"id", "netease:99"}, {"title", std::string(1, '\xff')}},
            Json{{"id", "netease:99"}, {"title", "Invalid"}, {"duration", -1}},
            Json{{"id", "netease:99"}, {"title", "Invalid"}, {"vipRequired", "true"}}}) {
        page = {{"tracks", Json::array({invalid})}};
        session.HandlePage(Command("home", request++));
        const auto result = Complete(session);
        Require(result["status"] == "error" && result["data"]["tracks"].empty(),
            "invalid public response cannot enter page or registry");
    }
}
void QQLikedCollectionProjection() {
    Fixture fixture;
    auto page = Page("qq:favorite");
    page["playlists"][0]["isLiked"] = true;
    fixture.answer = [&](std::string_view, const Json&, const Cancelled&) { return page; };
    MusicOnlineSession session(fixture.Options());
    std::uint64_t request = 0;
    session.HandlePage(Command("home", ++request, Json::object(), Provider::QQ));
    const auto result = Complete(session);
    Require(result["status"] == "ready" && result["data"]["playlists"][0]["isLiked"] == true &&
        result["data"]["playlists"][0].size() == 8,
        "QQ built-in liked collection identity survives public projection without native routing metadata");
    for (const Json& invalid : {Json("true"), Json(201)}) {
        page["playlists"][0]["isLiked"] = invalid;
        session.HandlePage(Command("home", ++request, Json::object(), Provider::QQ));
        Require(Complete(session)["error"] == "invalid-response", "liked collection marker is a boolean");
    }
    page["playlists"][0]["isLiked"] = true;
    page["playlists"][0]["owned"] = false;
    session.HandlePage(Command("home", ++request, Json::object(), Provider::QQ));
    Require(Complete(session)["error"] == "invalid-response", "subscribed QQ lists cannot identify account favorites");
}
void PagedRegistry() {
    Fixture fixture;
    Json page;
    fixture.answer = [&](std::string_view action, const Json&, const Cancelled&) {
        if (action == "resolve") return Json{{"url", "https://fixture.invalid/audio"}};
        if (action == "like") return Json{{"liked", true}};
        return page;
    };
    MusicOnlineSession session(fixture.Options());
    std::uint64_t request = 0;
    const auto read = [&](int offset, int start, int end, std::string query = "same") {
        page = {{"tracks", Json::array()}};
        for (int i = start; i < end; ++i)
            page["tracks"].push_back({{"id", "netease:" + std::to_string(i)}, {"title", "Fixture"}});
        session.HandlePage(Command("search", ++request, {{"offset", offset}, {"query", query}}));
        Require(Complete(session)["status"] == "ready", "paged read ready");
    };
    read(0, 0, 300);
    session.Cancel();
    read(300, 299, 600);
    session.HandlePage(Command("play", ++request, {{"id", "netease:2"}}));
    Require(fixture.played.size() == 512 && fixture.played.front().id == "netease:0" &&
        fixture.played.back().id == "netease:511", "append after page cancellation keeps old songs and deduplicates rows");
    const auto old = fixture.played.front();
    session.HandlePage(Command("play", ++request, {{"id", "netease:590"}}));
    Require(fixture.played.size() == 88 && fixture.played.front().id == "netease:512" &&
        fixture.played.back().id == "netease:599", "late song is included in bounded playback queue");
    session.HandlePage(Command("like", ++request, {{"id", "netease:2"}, {"value", true}}));
    Require(Complete(session)["status"] == "ready", "write completed");
    session.HandlePage(Command("play", ++request, {{"id", "netease:2"}}));
    Require(fixture.played.size() == 512, "writes cannot reset displayed authorization or queue");
    read(0, 800, 801);
    session.HandlePage(Command("play", ++request, {{"id", "netease:800"}}));
    Require(fixture.played.size() == 1, "offset zero resets displayed");
    Require(!session.ResolveSource(old, {}).empty(), "new read does not revoke background queued songs");
    read(100, 801, 802, "different");
    session.HandlePage(Command("play", ++request, {{"id", "netease:801"}}));
    Require(fixture.played.size() == 1, "query identity change resets even at a nonzero offset");
    ++fixture.generation;
    session.AccountsChanged();
    read(200, 802, 803, "different");
    session.HandlePage(Command("play", ++request, {{"id", "netease:802"}}));
    Require(fixture.played.size() == 1, "account generation changes reset displayed");
    Reject([&] { session.ResolveSource(old, {}); }, "old account queue cannot resolve after replacement");
}
void BackgroundLikedSnapshots() {
    Fixture fixture;
    auto page = Page();
    page["tracks"].push_back({{"id", "netease:43"}, {"title", "Second"}});
    Json snapshot;
    Provider expectedProvider = Provider::Netease;
    fixture.answer = [&](std::string_view action, const Json&, const Cancelled&) {
        Require(action == "search" || action == "likes", "background fixture uses reads only");
        return action == "likes" ? snapshot : page;
    };
    auto options = fixture.Options();
    const auto execute = options.execute;
    options.execute = [&](Provider provider, std::string_view action, const Json& args,
                          const Json& credentials, const Cancelled& stop) {
        if (action == "likes")
            Require(provider == expectedProvider && args == Json{{"_userId", "1"}},
                "background likes receives the requested provider's native lease without browsing arguments");
        return execute(provider, action, args, credentials, stop);
    };
    MusicOnlineSession session(std::move(options));
    std::uint64_t request = 1;
    session.HandlePage(Command("search", request, {{"query", "same"}}));
    Require(Complete(session)["status"] == "ready", "displayed list is established before background likes");
    for (const auto provider : {Provider::QQ, Provider::Kugou}) {
        expectedProvider = provider;
        const auto prefix = std::string(Name(provider)) + ":";
        const auto liked = prefix + (provider == Provider::QQ ? "liked" : std::string(32, 'A'));
        const auto injected = prefix + "injected";
        snapshot = Page(injected);
        snapshot["likedIds"] = Json::array({liked, liked});
        session.HandlePage(Command("likes", ++request, Json::object(), provider));
        const auto result = Complete(session);
        Require(result["status"] == "ready" && result["provider"] == Name(provider) &&
            result["action"] == "likes" && result["data"] == Json{{"tracks", Json::array()},
                {"playlists", Json::array()}, {"recommendations", Json::array()}, {"more", false},
                {"likedIds", Json::array({liked})}},
            "background likes publishes only a validated snapshot, never supplied lists");
        session.HandlePage(Command("play", ++request, {{"id", "netease:42"}}));
        Require(fixture.played.size() == 2 && fixture.played[0].id == "netease:42" &&
            fixture.played[1].id == "netease:43", "cross-provider likes cannot replace the displayed playback list");
        Require(Decode(session.HandlePage(Command("enqueue", ++request, {{"id", injected}}, provider)))["error"] ==
            "not-found", "likes cannot register an injected browsing track");
        Require(Decode(session.HandlePage(Command("enqueue", ++request, {{"id", liked}}, provider)))["error"] ==
            "not-found", "liked IDs alone do not grant playback registration");
    }
    page = {{"tracks", Json::array({{{"id", "netease:44"}, {"title", "Third"}}})}};
    session.HandlePage(Command("search", ++request, {{"query", "same"}, {"offset", 2}}));
    Require(Complete(session)["status"] == "ready", "original browsing read can append after other providers' likes");
    session.HandlePage(Command("play", ++request, {{"id", "netease:43"}}));
    Require(fixture.played.size() == 3 && fixture.played[0].id == "netease:42" &&
        fixture.played[1].id == "netease:43" && fixture.played[2].id == "netease:44",
        "likes preserves displayedRead identity and existing registration across subsequent page append");
    expectedProvider = Provider::QQ;
    for (const auto& invalid : {Json::object(), Json{{"likedIds", Json::array({"netease:42"})}}}) {
        snapshot = invalid;
        session.HandlePage(Command("likes", ++request, Json::object(), Provider::QQ));
        const auto result = Complete(session);
        Require(result["error"] == (invalid.empty() ? "unavailable" : "invalid-response") &&
            result["data"]["tracks"].empty(), "missing or foreign liked snapshots fail explicitly");
        session.HandlePage(Command("play", ++request, {{"id", "netease:43"}}));
        Require(fixture.played.size() == 3, "failed background likes also preserves the original playlist");
    }
}
void RegistryCapacityAndLikedIds() {
    Fixture fixture;
    Json page;
    fixture.answer = [&](std::string_view action, const Json&, const Cancelled&) {
        return action == "resolve" ? Json{{"url", "https://fixture.invalid/audio"}} : page;
    };
    MusicOnlineSession session(fixture.Options());
    std::uint64_t request = 0;
    for (int offset = 0; offset < 20000; offset += 100) {
        page = {{"tracks", Json::array()}};
        for (int i = offset; i < offset + 100; ++i)
            page["tracks"].push_back({{"id", "netease:" + std::to_string(i)}, {"title", "Fixture"}});
        session.HandlePage(Command("playlist", ++request, {{"id", "9"}, {"offset", offset}}));
        Require(Complete(session)["status"] == "ready", "reasonable long library remains registered");
    }
    page = {{"tracks", Json::array({{{"id", "netease:20000"}, {"title", "Overflow"}}})}};
    session.HandlePage(Command("playlist", ++request, {{"id", "9"}, {"offset", 20000}}));
    Require(Complete(session)["error"] == "unsupported", "registry limit is explicit, never clears old rows");
    session.HandlePage(Command("play", ++request, {{"id", "netease:19999"}}));
    Require(fixture.played.size() == 32 && fixture.played.back().id == "netease:19999",
        "overflow preserves the existing displayed queue");
    auto old = lume::music::Track{};
    old.id = "netease:0"; old.source = "online:" + old.id;
    Require(!session.ResolveSource(old, {}).empty(), "overflow preserves earliest authorization");
    page = {{"likedIds", Json::array({"netease:42", "netease:42", "netease:43"})}};
    session.HandlePage(Command("home", ++request));
    Require(Complete(session)["data"]["likedIds"] == Json::array({"netease:42", "netease:43"}),
        "public optional liked IDs are deduplicated without registering unbrowsed songs");
    for (const auto& invalid : {Json::array({"qq:42"}), Json::array({"netease:"}),
        Json::array({"netease:https://evil.test"}), Json::array({42}), Json("netease:42"),
        Json(Json::array_t(10001, Json("netease:42")))}) {
        page = {{"likedIds", invalid}};
        session.HandlePage(Command("home", ++request));
        Require(Complete(session)["error"] == "invalid-response", "unsafe liked snapshot rejected");
    }
}
void KugouHttpResolution() {
    Fixture fixture;
    MusicOnlineSession session(fixture.Options());
    lume::music::Track track;
    track.id = "kugou:" + std::string(32, 'A');
    track.source = "online:" + track.id;
    fixture.answer = [&](std::string_view, const Json&, const Cancelled&) { return Page(track.id); };
    session.HandlePage(Command("search", 1, {{"query", "fixture"}}, Provider::Kugou));
    Require(Complete(session)["status"] == "ready", "Kugou playback fixture is registered");
    const std::string url = "http://fs.youthandroid2.kugou.com/song.mp3?key=synthetic-secret";
    fixture.answer = [&](std::string_view action, const Json&, const Cancelled&) {
        Require(action == "resolve", "Kugou HTTP source still uses authorized native resolution");
        return Json{{"url", url}, {"quality", "standard"}, {"bitrate", 128000}};
    };
    const auto source = session.ResolveAudioSource(track, "standard", {});
    Require(source.url == url && source.current && source.current(),
        "Kugou HTTP source crosses the native lease boundary without protocol rewriting");
    for (const auto* unsafe : {"http://fs.kugou.com.evil.test/song", "http://user@fs.w.kugou.com/song",
            "http://127.0.0.1/song", "http://other.kugou.com/song", "http://fs.w.kugou.com:80/song",
            "http://fs.w.kugou.com/song\r\nInjected: x"}) {
        fixture.answer = [&](std::string_view, const Json&, const Cancelled&) { return Json{{"url", unsafe}}; };
        Reject([&] { session.ResolveSource(track, {}); }, "Kugou HTTP exception rejects non-CDN or malformed sources");
    }
    fixture.signedIn = false;
    Require(!source.current(), "logout revokes the resolved Kugou HTTP source lease");
}
void QualityResolution() {
    Fixture fixture;
    MusicOnlineSession session(fixture.Options());
    const auto track = Known(session);
    fixture.answer = [](std::string_view action, const Json& args, const Cancelled&) {
        Require(action == "resolve" && args["quality"] == "hires", "requested quality reaches authorized native resolver");
        return Json{{"url", "https://fixture.invalid/audio?secret=fixture"}, {"quality", "lossless"}, {"bitrate", 900000}};
    };
    const auto source = session.ResolveAudioSource(track, "hires", {});
    Require(source.quality == "lossless" && source.bitrate == 900000,
        "native resolver retains actual downgrade without exposing source to page");
    for (const auto* quality : {"jyeffect", "sky", "jymaster"}) {
        fixture.answer = [&](std::string_view action, const Json& args, const Cancelled&) {
            Require(action == "resolve" && args["quality"] == quality, "NetEase exclusive level reaches resolver");
            return Json{{"url", "https://fixture.invalid/audio"}, {"quality", quality}, {"bitrate", 2400000}};
        };
        Require(session.ResolveAudioSource(track, quality, {}).quality == quality,
            "NetEase exclusive actual quality survives native boundary");
    }
    Reject([&] { session.ResolveAudioSource(track, "auto", {}); }, "unknown native quality rejected");
    for (const auto& invalid : {Json{{"quality", "auto"}}, Json{{"bitrate", -1}}, Json{{"bitrate", 10000001}},
        Json{{"bitrate", "320000"}}, Json{{"url", "https://user@fixture.invalid/audio"}}}) {
        fixture.answer = [&](std::string_view, const Json&, const Cancelled&) {
            Json value = {{"url", "https://fixture.invalid/audio"}, {"quality", "high"}, {"bitrate", 320000}};
            value.update(invalid); return value;
        };
        Reject([&] { session.ResolveAudioSource(track, "high", {}); }, "unsafe audio result rejected");
    }
    for (const auto provider : {Provider::QQ, Provider::Kugou}) {
        Fixture other;
        MusicOnlineSession providerSession(other.Options());
        lume::music::Track providerTrack;
        providerTrack.id = std::string(Name(provider)) + ":42";
        providerTrack.source = "online:" + providerTrack.id;
        other.answer = [&](std::string_view, const Json&, const Cancelled&) { return Page(providerTrack.id); };
        providerSession.HandlePage(Command("search", 1, {{"query", "fixture"}}, provider));
        Require(Complete(providerSession)["status"] == "ready", "other provider quality fixture is registered");
        const int calls = other.calls;
        for (const auto* quality : {"jyeffect", "sky", "jymaster"})
            Reject([&] { providerSession.ResolveAudioSource(providerTrack, quality, {}); },
                "QQ and Kugou reject NetEase-exclusive requested qualities");
        Require(other.calls == calls, "provider quality rejection occurs before execute");
        other.answer = [](std::string_view, const Json&, const Cancelled&) {
            return Json{{"url", "https://fixture.invalid/audio"}, {"quality", "jymaster"}};
        };
        Reject([&] { providerSession.ResolveAudioSource(providerTrack, "high", {}); },
            "other providers cannot advertise a NetEase-exclusive actual quality");
    }
}

void SourceLeaseLifetime() {
    for (const bool shutdown : {false, true}) {
        Fixture fixture;
        auto options = fixture.Options();
        options.acquire = [&](Provider provider) -> std::optional<MusicProviderCredentialLease> {
            if (!fixture.signedIn) return {};
            return MusicProviderCredentialLease{provider, fixture.generation.load(), "same-user",
                {{"token", "synthetic-secret"}}};
        };
        MusicProviderCredentialLease checked;
        int checks = 0;
        options.current = [&](const MusicProviderCredentialLease& lease) {
            ++checks; checked = lease;
            return fixture.signedIn && lease.generation == fixture.generation && lease.userId == "same-user";
        };
        auto session = std::make_unique<MusicOnlineSession>(std::move(options));
        auto track = Known(*session);
        bool stopped = false;
        const auto source = session->ResolveAudioSource(track, "standard", [&] { return stopped; });
        stopped = true;
        Require(source.current && source.current(), "resolved source retains an independent lease guard");
        Require(checked.provider == Provider::Netease && checked.generation == 1 &&
            checked.userId == "same-user" && checked.credentials.is_object() && checked.credentials.empty(),
            "source guard retains resolving identity without credentials or caller cancellation");
        session->Cancel();
        Require(source.current(), "page cancellation does not revoke a background playback source");
        fixture.signedIn = false;
        Require(!source.current(), "logout revokes the resolved source before host ACK");
        fixture.signedIn = true;
        ++fixture.generation;
        Require(!source.current(), "same-user replacement generation cannot authorize the old URL");
        session->HandlePage(Command("search", 2, {{"query", "fixture"}}));
        Require(Complete(*session)["status"] == "ready", "replacement identity registers a fresh track");
        const auto fresh = session->ResolveAudioSource(track, "standard", {});
        Require(fresh.current && fresh.current() && !source.current(), "fresh source alone uses the new lease");
        if (shutdown) session->Shutdown();
        else session.reset();
        const int before = checks;
        Require(!fresh.current() && !source.current() && checks == before,
            "closed or destroyed online session fails safely without invoking account callbacks");
    }
}
}
int main(int argc, char** argv) {
    const std::string test = argc > 1 ? argv[1] : "";
    if (test.empty() || test == "busy") BusyWrites();
    if (test.empty() || test == "supersede") SupersededReadAndInvalidPlay();
    if (test.empty() || test == "registry") ProjectionAndRegistry();
    if (test.empty() || test == "account") {
        AccountChangeDuringRead();
        AccountChangeDuringRead("likes");
    }
    if (test.empty() || test == "shutdown") ShutdownResolution();
    if (test.empty() || test == "cancel") CancellationAndErrors();
    if (test.empty() || test == "parser") Parser();
    if (test.empty() || test == "acquire") AccountChangeDuringAcquire();
    if (test.empty() || test == "projection") PublicDataValidation();
    if (test.empty() || test == "projection") QQLikedCollectionProjection();
    if (test.empty() || test == "paging") PagedRegistry();
    if (test.empty() || test == "paging" || test == "likes") BackgroundLikedSnapshots();
    if (test.empty() || test == "capacity") RegistryCapacityAndLikedIds();
    if (test.empty() || test == "quality") QualityResolution();
    if (test.empty() || test == "kugou") KugouHttpResolution();
    if (test.empty() || test == "lease") SourceLeaseLifetime();
    std::cout << "music online session isolated regressions passed\n";
}
