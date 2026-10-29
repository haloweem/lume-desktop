#include "apps/shell/music_online_session.h"
#include "platform/windows/music_online_provider.h"
#include "core/music/music_online_lyrics.h"
#include "core/music/music_model.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace lume::shell {
namespace {
using namespace music::auth;
constexpr std::uint64_t MaxCounter = 9007199254740991ULL;
constexpr std::size_t MaxRegisteredTracks = 20000;
// Only browsing reads replace the displayed list; background likes use the same worker without doing so.
const std::set<std::string> Reads{"home", "explore", "playlist", "search", "favorites", "recent"};
const std::set<std::string> Writes{"like", "createPlaylist", "addToPlaylist"};
bool Text(const Json& value, std::size_t max, bool empty = false) {
    if (!value.is_string()) return false;
    const auto& text = value.get_ref<const std::string&>();
    return (empty || !text.empty()) && text.size() <= max &&
        (text.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
            static_cast<int>(text.size()), nullptr, 0) > 0) &&
        std::none_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
std::wstring Wide(const std::string& value) {
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) throw std::runtime_error("invalid-response");
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), size);
    return result;
}
std::optional<Provider> ParseProvider(std::string_view name) {
    for (const auto provider : {Provider::Netease, Provider::QQ, Provider::Kugou})
        if (Name(provider) == name) return provider;
    return {};
}
std::string Error(std::string_view value) {
    for (const auto* allowed : {"network", "service", "expired", "unsupported", "unavailable",
            "invalid-response", "storage", "signed-out", "busy", "not-found"})
        if (value == allowed) return allowed;
    return "service";
}
bool MembershipBadge(std::string_view value) {
    return value.empty() || value == "qq-green-vip" || value == "qq-super-vip" ||
        value == "netease-vip" || value == "netease-svip" || value == "kugou-vip";
}
bool PageId(std::string_view id) {
    return !id.empty() && id.size() <= 128 &&
        id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_:-.") == id.npos;
}
Json EmptyData() {
    return {{"tracks", Json::array()}, {"playlists", Json::array()},
        {"recommendations", Json::array()}, {"more", false}};
}
Json PublicData(const Json& raw, Provider provider, std::vector<music::Track>& tracks) {
    if (!raw.is_object()) throw std::runtime_error("invalid-response");
    auto output = EmptyData();
    const std::string prefix = std::string(Name(provider)) + ":";
    if (raw.contains("tracks")) {
        if (!raw["tracks"].is_array() || raw["tracks"].size() > 512) throw std::runtime_error("invalid-response");
        std::set<std::string> seen;
        for (const auto& item : raw["tracks"]) {
            if (!item.is_object() || !Text(item.value("id", Json{}), 128) ||
                !Text(item.value("title", Json{}), 1024)) throw std::runtime_error("invalid-response");
            music::Track track;
            track.id = item["id"].get<std::string>();
            if (!PageId(track.id) || track.id.compare(0, prefix.size(), prefix) != 0 ||
                track.id.size() == prefix.size()) throw std::runtime_error("invalid-response");
            if (!seen.insert(track.id).second) continue;
            track.title = item["title"].get<std::string>();
            for (const auto* key : {"artist", "album", "artwork"})
                if (!Text(item.value(key, Json("")), key == std::string_view("artwork") ? 2048 : 1024, true))
                    throw std::runtime_error("invalid-response");
            track.artist = item.value("artist", "");
            track.album = item.value("album", "");
            track.artwork = item.value("artwork", "");
            track.duration = item.value("duration", 0.0);
            if (!std::isfinite(track.duration) || track.duration < 0 || track.duration > 86400)
                throw std::runtime_error("invalid-response");
            if (item.contains("vipRequired") && !item["vipRequired"].is_boolean())
                throw std::runtime_error("invalid-response");
            track.vipRequired = item.value("vipRequired", false);
            if (item.contains("membershipBadge") &&
                (!item["membershipBadge"].is_string() || !MembershipBadge(item["membershipBadge"].get<std::string>())))
                throw std::runtime_error("invalid-response");
            track.membershipBadge = item.value("membershipBadge", "");
            track.source = "online:" + track.id;
            auto publicTrack = Json{{"id", track.id}, {"title", track.title}, {"artist", track.artist},
                {"album", track.album}, {"artwork", track.artwork}, {"duration", track.duration},
                {"source", track.source}, {"vipRequired", track.vipRequired}};
            if (!track.membershipBadge.empty()) publicTrack["membershipBadge"] = track.membershipBadge;
            output["tracks"].push_back(std::move(publicTrack));
            tracks.push_back(std::move(track));
        }
    }
    for (const auto* key : {"playlists", "recommendations"}) if (raw.contains(key)) {
        if (!raw[key].is_array() || raw[key].size() > 1000) throw std::runtime_error("invalid-response");
        for (const auto& item : raw[key]) {
            if (!item.is_object() || !Text(item.value("id", Json{}), 128) ||
                !PageId(item["id"].get<std::string>()) || !Text(item.value("title", Json{}), 1024) ||
                !Text(item.value("artwork", Json("")), 2048, true) ||
                !Text(item.value("description", Json("")), 2048, true)) throw std::runtime_error("invalid-response");
            auto collection = Json{{"id", item["id"]}, {"providerId", Name(provider)},
                {"type", item.value("type", "") == "album" ? "album" : "playlist"}, {"title", item["title"]},
                {"artwork", item.value("artwork", "")}, {"description", item.value("description", "")},
                {"owned", item.value("owned", false)}};
            if (item.contains("isLiked")) {
                if (!item["isLiked"].is_boolean() || provider != Provider::QQ ||
                    !item.value("owned", false)) throw std::runtime_error("invalid-response");
                collection["isLiked"] = item["isLiked"];
            }
            output[key].push_back(std::move(collection));
        }
    }
    output["more"] = raw.value("more", false);
    if (raw.contains("liked")) output["liked"] = raw.at("liked").get<bool>();
    if (raw.contains("likedIds")) {
        const auto& ids = raw.at("likedIds");
        if (!ids.is_array() || ids.size() > 10000) throw std::runtime_error("invalid-response");
        output["likedIds"] = Json::array();
        std::set<std::string> seen;
        for (const auto& value : ids) {
            if (!Text(value, 128)) throw std::runtime_error("invalid-response");
            const auto id = value.get<std::string>();
            if (!PageId(id) || id.compare(0, prefix.size(), prefix) != 0 || id.size() == prefix.size())
                throw std::runtime_error("invalid-response");
            if (seen.insert(id).second) output["likedIds"].push_back(id);
        }
    }
    return output;
}
}

std::optional<MusicOnlineCommand> ParseMusicOnlineCommand(std::wstring_view input) noexcept {
    try {
        if (input.empty() || input.size() > 4096) return {};
        bool invalid = false;
        std::set<std::string> keys;
        auto json = Json::parse(input.begin(), input.end(), [&](int depth, Json::parse_event_t event, Json& value) {
            if (depth > 2) invalid = true;
            if (event == Json::parse_event_t::key && !keys.insert(value.get<std::string>()).second) invalid = true;
            return true;
        }, false);
        if (invalid || !json.is_object() || !json.at("version").is_number_integer() ||
            json.at("version") != 1 || json.at("scope") != "music-online" ||
            !json.at("request").is_number_unsigned()) return {};
        MusicOnlineCommand command;
        command.request = json.at("request").get<std::uint64_t>();
        command.action = json.at("action").get<std::string>();
        if (!command.request || command.request > MaxCounter) return {};
        const auto provider = ParseProvider(json.at("provider").get<std::string>());
        if (!provider) return {};
        command.provider = *provider;
        std::set<std::string> allowed{"version", "scope", "action", "request", "provider"};
        if (Reads.count(command.action)) {
            allowed.insert("offset"); allowed.insert("limit");
            for (const auto* key : {"offset", "limit"}) if (json.contains(key)) {
                if (!json[key].is_number_unsigned()) return {};
                const auto number = json[key].get<std::uint64_t>();
                if ((std::string_view(key) == "offset" && number > 100000) ||
                    (std::string_view(key) == "limit" && (!number || number > 100))) return {};
                command.arguments[key] = number;
            }
            if (command.action == "playlist") allowed.insert("id");
            if (command.action == "search") {
                allowed.insert("query");
                if (!Text(json.value("query", Json{}), 512)) return {};
                command.arguments["query"] = json["query"];
            }
        } else if (command.action == "like") { allowed.insert("id"); allowed.insert("value"); }
        else if (command.action == "createPlaylist") {
            allowed.insert("title");
            if (!Text(json.value("title", Json{}), 120)) return {};
            command.arguments["title"] = json["title"];
        } else if (command.action == "addToPlaylist") { allowed.insert("id"); allowed.insert("trackId"); }
        else if (command.action == "play" || command.action == "enqueue" || command.action == "insertNext")
            allowed.insert("id");
        else if (command.action != "cancel" && command.action != "likes") return {};
        for (const auto* key : {"id", "trackId"}) if (allowed.count(key)) {
            if (!Text(json.value(key, Json{}), 128) || !PageId(json[key].get<std::string>())) return {};
            command.arguments[key] = json[key];
        }
        if (allowed.count("value")) {
            if (!json.value("value", Json{}).is_boolean()) return {};
            command.arguments["value"] = json["value"];
        }
        for (const auto& item : json.items()) if (!allowed.count(item.key())) return {};
        return command;
    } catch (...) { return {}; }
}

struct MusicOnlineSession::Impl {
    explicit Impl(MusicOnlineOptions value) : options(std::move(value)) {}
    ~Impl() { Shutdown(); }
    struct Registered { music::Track track; MusicProviderCredentialLease identity; };
    // Registration retains identity only, not credentials.
    static MusicProviderCredentialLease Identity(MusicProviderCredentialLease lease) {
        lease.credentials = Json::object();
        return lease;
    }
    bool Current(const MusicProviderCredentialLease& lease) const {
        return !closed && options.current && options.current(lease);
    }
    Json Execute(const MusicProviderCredentialLease& lease, std::string_view action, Json args,
                 const Cancelled& cancelled) {
        CheckCancelled(cancelled);
        args["_userId"] = lease.userId;
        if (options.execute) return options.execute(lease.provider, action, args, lease.credentials, cancelled);
        if (options.verification) throw std::runtime_error("service");
        return music::online::Execute(lease.provider, action, args, lease.credentials, cancelled);
    }
    static std::wstring State(const MusicOnlineCommand& command, std::string_view status,
                              std::string_view error, const Json& data) {
        return Wide(Json{{"version", 1}, {"type", "music-online-state"}, {"request", command.request},
            {"provider", Name(command.provider)}, {"action", command.action}, {"status", status},
            {"error", error}, {"data", data}}.dump());
    }
    std::wstring StateLocked() const { return State(last, status, error, data); }
    void NotifyLocked() { pending = true; if (options.wake) options.wake(); }
    std::wstring HandlePage(const MusicOnlineCommand& command) {
        std::unique_lock lock(mutex);
        if (closed || command.request <= lastRequest || command.request > MaxCounter) return StateLocked();
        lastRequest = command.request;
        // A busy reply belongs to the rejected command, not the durable write.
        if (status == "loading" && Writes.count(last.action) && command.action != "cancel")
            return State(command, "error", "busy", EmptyData());
        ++generation;
        job.reset(); activeIdentity.reset(); publishedIdentity.reset(); pending = false;
        if (command.action == "play" || command.action == "enqueue" || command.action == "insertNext") {
            const auto found = registered.find(command.arguments.value("id", ""));
            if (found == registered.end() || found->second.identity.provider != command.provider ||
                !Current(found->second.identity)) {
                last = command; status = "error"; error = "not-found"; data = EmptyData();
                return StateLocked();
            }
            std::vector<music::Track> selected;
            if (command.action == "play") {
                for (const auto& id : displayed) {
                    const auto track = registered.find(id);
                    if (track != registered.end() && track->second.identity.provider == command.provider &&
                        Current(track->second.identity)) selected.push_back(track->second.track);
                }
                // The playback model owns a bounded queue, not the entire browsing registry.
                const auto clicked = std::find_if(selected.begin(), selected.end(),
                    [&](const auto& track) { return track.id == found->first; });
                if (clicked != selected.end() && selected.size() > music::kMaxMusicTracks) {
                    const auto start = static_cast<std::size_t>(clicked - selected.begin()) /
                        music::kMaxMusicTracks * music::kMaxMusicTracks;
                    selected = std::vector<music::Track>(selected.begin() + start,
                        selected.begin() + std::min(selected.size(), start + music::kMaxMusicTracks));
                }
            }
            if (selected.empty() || std::none_of(selected.begin(), selected.end(),
                [&](const auto& track) { return track.id == found->first; })) selected = {found->second.track};
            const auto id = found->first;
            last = command; status = "ready"; error.clear(); data = EmptyData();
            const auto message = StateLocked();
            lock.unlock();
            if (options.play) options.play(std::move(selected), id, command.action);
            return message;
        }
        last = command;
        error.clear(); data = EmptyData();
        if (command.action == "cancel") { status = "idle"; return StateLocked(); }
        status = "loading";
        job = std::make_pair(command, generation.load());
        if (worker.joinable() && finished) worker.join();
        if (!worker.joinable()) {
            finished = false;
            worker = std::thread([this] { Run(); });
        }
        return StateLocked();
    }
    void Run() {
        for (;;) {
            MusicOnlineCommand command;
            std::uint64_t epoch;
            {
                std::lock_guard lock(mutex);
                if (closed || !job) { finished = true; return; }
                command = std::move(job->first); epoch = job->second; job.reset();
            }
            std::string failure;
            Json result = EmptyData();
            std::vector<music::Track> tracks;
            std::optional<MusicProviderCredentialLease> lease;
            try {
                lease = options.acquire ? options.acquire(command.provider) : std::nullopt;
                if (!lease || lease->provider != command.provider) throw std::runtime_error("signed-out");
                {
                    std::lock_guard lock(mutex);
                    if (closed || epoch != generation.load()) continue;
                    activeIdentity = Identity(*lease);
                }
                const auto cancelled = [&] { return epoch != generation.load() || !Current(*lease); };
                auto raw = Execute(*lease, command.action, command.arguments, cancelled);
                if (command.action == "likes") {
                    if (!raw.contains("likedIds")) throw std::runtime_error("unavailable");
                    // Background snapshots cannot publish or register a browsing list.
                    raw = Json{{"likedIds", raw.at("likedIds")}};
                }
                result = PublicData(raw, command.provider, tracks);
                CheckCancelled(cancelled);
            } catch (const std::exception& exception) { failure = Error(exception.what()); }
            catch (...) { failure = "service"; }
            std::lock_guard lock(mutex);
            if (closed || epoch != generation.load()) continue;
            activeIdentity.reset();
            if (lease && !Current(*lease)) { result = EmptyData(); failure = "signed-out"; }
            if (failure.empty() && lease) {
                const auto added = std::count_if(tracks.begin(), tracks.end(),
                    [&](const auto& track) { return !registered.count(track.id); });
                if (registered.size() + added > MaxRegisteredTracks) {
                    result = EmptyData(); failure = "unsupported";
                } else {
                    publishedIdentity = Identity(*lease);
                    if (Reads.count(command.action)) {
                        const bool append = command.arguments.value("offset", 0u) > 0 && displayedRead &&
                            displayedRead->provider == command.provider && displayedRead->action == command.action &&
                            displayedRead->arguments.value("query", "") == command.arguments.value("query", "") &&
                            displayedRead->arguments.value("id", "") == command.arguments.value("id", "") &&
                            displayedIdentity && displayedIdentity->generation == lease->generation &&
                            displayedIdentity->userId == lease->userId;
                        if (!append) displayed.clear();
                        displayedRead = command; displayedIdentity = Identity(*lease);
                        std::set<std::string> seen(displayed.begin(), displayed.end());
                        for (auto& track : tracks) {
                            if (seen.insert(track.id).second) displayed.push_back(track.id);
                            const auto id = track.id;
                            registered[id] = {std::move(track), Identity(*lease)};
                        }
                    }
                }
            }
            data = std::move(result);
            error = std::move(failure);
            status = error.empty() ? "ready" : "error";
            NotifyLocked();
        }
    }
    std::pair<MusicProviderCredentialLease, std::string> TrackLease(const music::Track& track) {
        if (track.source != "online:" + track.id || !PageId(track.id)) throw std::runtime_error("not-found");
        const auto colon = track.id.find(':');
        if (colon == std::string::npos || colon + 1 == track.id.size()) throw std::runtime_error("not-found");
        const auto provider = ParseProvider(std::string_view(track.id).substr(0, colon));
        if (!provider) throw std::runtime_error("not-found");
        MusicProviderCredentialLease identity;
        {
            std::lock_guard lock(mutex);
            const auto found = registered.find(track.id);
            if (closed || found == registered.end() || found->second.identity.provider != *provider)
                throw std::runtime_error("not-found");
            identity = found->second.identity;
            if (!Current(identity)) throw std::runtime_error("signed-out");
        }
        auto lease = options.acquire ? options.acquire(*provider) : std::nullopt;
        if (!lease || lease->provider != identity.provider || lease->generation != identity.generation ||
            lease->userId != identity.userId || !Current(*lease)) throw std::runtime_error("signed-out");
        return {std::move(*lease), track.id.substr(colon + 1)};
    }
    void AccountsChanged() {
        std::lock_guard lock(mutex);
        for (auto it = registered.begin(); it != registered.end();)
            if (!Current(it->second.identity)) it = registered.erase(it); else ++it;
        if (displayedIdentity && !Current(*displayedIdentity)) {
            displayed.clear(); displayedRead.reset(); displayedIdentity.reset();
        }
        // Settle revoked work immediately, even if transport has not returned yet.
        if ((status == "loading" && (!activeIdentity || !Current(*activeIdentity))) ||
            (publishedIdentity && !Current(*publishedIdentity))) {
            ++generation; job.reset(); activeIdentity.reset();
            data = EmptyData(); error = "signed-out"; status = "error"; NotifyLocked();
            publishedIdentity.reset();
        }
    }
    void Cancel() {
        std::lock_guard lock(mutex);
        ++generation; job.reset(); activeIdentity.reset(); data = EmptyData();
        // Page visibility preserves both the browsing list and background queue.
        publishedIdentity.reset();
        status = "idle"; error.clear(); pending = false;
    }
    void Shutdown() {
        std::unique_lock lock(mutex);
        if (closed) return;
        closed = true; ++generation; job.reset(); registered.clear(); displayed.clear();
        activeIdentity.reset(); publishedIdentity.reset(); data = EmptyData();
        status = "idle"; error.clear(); pending = false;
        auto retiring = std::move(worker);
        lock.unlock();
        if (retiring.joinable()) retiring.join();
    }
    MusicOnlineOptions options;
    mutable std::mutex mutex;
    std::atomic<std::uint64_t> generation{0};
    std::optional<std::pair<MusicOnlineCommand, std::uint64_t>> job;
    std::map<std::string, Registered> registered;
    std::vector<std::string> displayed;
    std::optional<MusicOnlineCommand> displayedRead;
    std::optional<MusicProviderCredentialLease> displayedIdentity;
    std::optional<MusicProviderCredentialLease> publishedIdentity;
    std::optional<MusicProviderCredentialLease> activeIdentity;
    std::thread worker;
    MusicOnlineCommand last;
    std::uint64_t lastRequest = 0;
    Json data = EmptyData();
    std::string status = "idle", error;
    bool pending = false, finished = true;
    std::atomic<bool> closed{false};
};

MusicOnlineSession::MusicOnlineSession(MusicOnlineOptions options) : impl_(std::make_shared<Impl>(std::move(options))) {}
MusicOnlineSession::~MusicOnlineSession() { impl_->Shutdown(); }
std::wstring MusicOnlineSession::HandlePage(const MusicOnlineCommand& command) { return impl_->HandlePage(command); }
std::optional<std::wstring> MusicOnlineSession::DrainNotification() {
    std::lock_guard lock(impl_->mutex);
    if (impl_->closed || !impl_->pending) return {};
    impl_->pending = false;
    return impl_->StateLocked();
}
std::string MusicOnlineSession::ResolveSource(const music::Track& track, const Cancelled& stop) {
    return ResolveAudioSource(track, "standard", stop).url;
}
music::ResolvedAudioSource MusicOnlineSession::ResolveAudioSource(const music::Track& track,
    std::string_view quality, const Cancelled& stop) {
    if (!music::ValidAudioQuality(quality)) throw std::runtime_error("unsupported");
    auto [lease, id] = impl_->TrackLease(track);
    if (!music::ValidAudioQuality(quality, Name(lease.provider))) throw std::runtime_error("unsupported");
    const auto cancelled = [&] { return (stop && stop()) || !impl_->Current(lease); };
    auto result = impl_->Execute(lease, "resolve", {{"id", id}, {"quality", quality}}, cancelled);
    CheckCancelled(cancelled);
    const auto url = result.at("url").get<std::string>();
    if (url.size() > 8192 || (url.compare(0, 8, "https://") != 0 &&
        !(lease.provider == Provider::Kugou && music::IsKugouHttpAudioUrl(url))) ||
        url.find_first_of("\r\n\t\\ @#") != url.npos ||
        std::any_of(url.begin(), url.end(), [](unsigned char c) { return c < 33 || c >= 127; }))
        throw std::runtime_error("unavailable");
    const auto actual = result.value("quality", std::string{});
    const auto bitrate = result.value("bitrate", Json(0));
    if ((!actual.empty() && !music::ValidAudioQuality(actual, Name(lease.provider))) || !bitrate.is_number_integer() ||
        bitrate < 0 || bitrate > 10000000) throw std::runtime_error("invalid-response");
    return {url, actual, bitrate.get<std::uint32_t>(),
        [owner = std::weak_ptr<Impl>(impl_), identity = Impl::Identity(std::move(lease))] {
            const auto session = owner.lock();
            try { return session && session->Current(identity); }
            catch (...) { return false; }
        }};
}
music::LocalLyricsResult MusicOnlineSession::LoadLyrics(const music::Track& track, const Cancelled& stop) {
    try {
        auto [lease, id] = impl_->TrackLease(track);
        const auto cancelled = [&] { return (stop && stop()) || !impl_->Current(lease); };
        const auto raw = impl_->Execute(lease, "lyrics", {{"id", id}}, cancelled);
        CheckCancelled(cancelled);
        return music::ParseOnlineLyrics(Name(lease.provider), raw, track.duration);
    } catch (const std::exception& exception) { return {{}, Error(exception.what())}; }
}
void MusicOnlineSession::AccountsChanged() { impl_->AccountsChanged(); }
void MusicOnlineSession::Cancel() { impl_->Cancel(); }
void MusicOnlineSession::Shutdown() { impl_->Shutdown(); }
bool MusicOnlineSession::HasWork() const {
    std::lock_guard lock(impl_->mutex);
    return !impl_->closed && (!impl_->finished || impl_->pending);
}
} // namespace lume::shell
