#include "platform/windows/music_online_provider.h"
#include "core/music/music_online_lyrics.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <set>

using lume::music::auth::Json;
using lume::music::auth::Provider;
using lume::music::auth::HttpRequest;
using lume::music::auth::HttpResponse;
using lume::music::auth::Cancelled;
using lume::music::auth::Exchange;
using lume::music::auth::Name;
using lume::music::auth::Aes;
using lume::music::auth::HashHex;
using lume::music::auth::ValidHttpRequest;
using lume::music::online::Execute;
using lume::music::ParseOnlineLyrics;
namespace {
const std::string lineLyrics = "[00:01.250]First line\n[00:03.750]Second line";
void Expect(bool condition, const char* label) {
    if (!condition) { std::cerr << label << '\n'; std::exit(1); }
}
template<class F> void Error(F action, std::string_view expected) {
    try { action(); }
    catch (const std::exception& e) {
        if (e.what() != expected) {
            std::cerr << "Expected " << expected << ", got " << e.what() << '\n';
            std::exit(1);
        }
        return;
    }
    Expect(false, "Expected failure");
}
std::string Decode(std::string_view encoded) {
    std::string result;
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] == '%') {
            Expect(i + 2 < encoded.size(), "escaped query");
            result += static_cast<char>(std::stoul(std::string(encoded.substr(i + 1, 2)), nullptr, 16));
            i += 2;
        } else result += encoded[i];
    }
    return result;
}
std::map<std::string, std::string> Query(const std::string& url) {
    std::map<std::string, std::string> result;
    auto start = url.find('?');
    if (start == url.npos) return result;
    ++start;
    while (start < url.size()) {
        const auto end = url.find('&', start);
        const auto equal = url.find('=', start);
        Expect(equal != url.npos, "query pair");
        result[Decode(std::string_view(url).substr(start, equal - start))] =
            Decode(std::string_view(url).substr(equal + 1,
                end == url.npos ? url.size() - equal - 1 : end - equal - 1));
        if (end == url.npos) break;
        start = end + 1;
    }
    return result;
}
Json Eapi(const HttpRequest& request) {
    auto hex = request.body.substr(7);
    std::string bytes;
    for (std::size_t i = 0; i < hex.size(); i += 2)
        bytes += static_cast<char>(std::stoul(hex.substr(i, 2), nullptr, 16));
    const auto plain = Aes(bytes, "e82ckenh8dichen8", "", false);
    const std::string marker = "-36cd479b6b5-";
    const auto first = plain.find(marker), last = plain.rfind(marker);
    Expect(first != plain.npos && first != last, "eapi envelope");
    const auto path = plain.substr(0, first);
    const auto data = plain.substr(first + marker.size(), last - first - marker.size());
    Expect(HashHex("nobody" + path + "use" + data + "md5forencrypt") ==
        plain.substr(last + marker.size()), "eapi digest");
    auto json = Json::parse(data);
    Expect(json["e_r"] == false, "bounded plaintext response");
    return json;
}
HttpResponse Response(Json body) {
    return {200, body.dump(), "application/json", {}, {}};
}
Json TrackN(int id = 42) {
    return {{"id", id}, {"name", "Fixture"}, {"ar", Json::array({{{"name", "Artist"}}})},
        {"al", {{"name", "Album"}, {"picUrl", "http://p1.music.126.net/art.jpg"}}}, {"dt", 123456}, {"fee", 1}};
}
Json TrackQ() {
    return {{"mid", "songMID"}, {"id", 123}, {"type", 0}, {"name", "Fixture"},
        {"singer", Json::array({{{"name", "Artist"}}})},
        {"album", {{"name", "Album"}, {"mid", "albumMID"}}}, {"interval", 123},
        {"file", {{"media_mid", "mediaMID"}}}, {"pay", {{"pay_play", 1}}},
        {"url", "https://secret.invalid/leak"}};
}
const std::string hash(32, 'A');
Json TrackK() {
    return {{"FileHash", hash}, {"SongName", "Artist - Fixture"}, {"SingerName", "Artist"},
        {"AlbumName", "Album"}, {"AlbumID", 22}, {"AlbumAudioID", 33}, {"MixSongID", 44},
        {"fileid", 999}, {"Duration", 123}, {"pay_type", 3},
        {"Image", "http://imge.kugou.com/{size}/art.jpg"}};
}
Json Credentials(Provider provider) {
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() - 2000;
    if (provider == Provider::Netease)
        return {{"MUSIC_U", "synthetic-netease-secret"}, {"__csrf", "synthetic-csrf"}};
    if (provider == Provider::Kugou)
        return {{"userid", "77"}, {"token", "synthetic-kugou-secret"}, {"dfid", "synthetic-dfid"},
            {"KUGOU_API_GUID", std::string(32, 'A')}, {"KUGOU_API_MID", "123456"}};
    return {{"credential", {{"uin", "77"}, {"musicKey", "synthetic-qq-secret"}, {"loginType", 2}}},
        {"device", {{"openUdid", "synthetic-udid"}, {"qimei16", "synthetic-q16"},
            {"qimei36", "synthetic-q36"}, {"qimeiUpdatedAt", now}, {"sessionUid", "uid"},
            {"sessionSid", "sid"}, {"sessionUpdatedAt", now}}}};
}
void TrackSchema(const Json& page, Provider provider) {
    Expect(page["tracks"].is_array() && page["playlists"].is_array() &&
        page["recommendations"].is_array() && page["more"].is_boolean(), "fixed page schema");
    Expect(page["tracks"].size() <= 100 && page["playlists"].size() <= 100 &&
        page["recommendations"].size() <= 100, "bounded page");
    for (const auto& track : page["tracks"]) {
        Expect(track.size() == (track["vipRequired"] ? 9 : 8),
            "membership badge is present only for paid tracks");
        const auto id = track["id"].get<std::string>();
        Expect(id.find(std::string(Name(provider)) + ":") == 0 && track["source"] == "online:" + id,
            "stable ID and source");
        Expect(track["title"] == "Fixture" && track["artist"] == "Artist" &&
            track["album"] == "Album", "normalized metadata");
        Expect(track["duration"].is_number() && track["duration"] > 0, "duration seconds");
        Expect(track["vipRequired"].is_boolean(), "paid state is explicit");
        if (track["vipRequired"]) {
            const auto expectedBadge = provider == Provider::Netease ? "netease-vip" :
                provider == Provider::QQ ? "qq-green-vip" : "kugou-vip";
            Expect(track["membershipBadge"] == expectedBadge,
                "provider membership badge is normalized");
        } else {
            Expect(!track.contains("membershipBadge"), "free tracks have no membership badge");
        }
    }
    Expect(page.dump().find("secret") == std::string::npos, "no credentials or wire URL leaked");
}
void CollectionSchema(const Json& page) {
    for (const auto* key : {"playlists", "recommendations"})
        for (const auto& item : page[key])
            Expect(item.size() == (item.contains("isLiked") ? 8 : 7) &&
                (!item.contains("isLiked") || (item["providerId"] == "qq" && item["owned"] == true &&
                    item["isLiked"] == true)) && item.contains("providerId") && !item.contains("provider") &&
                item["owned"].is_boolean() && item["type"].is_string() &&
                item["description"].is_string(), "collection canonical schema");
}
struct Fixture {
    Provider provider;
    Json credentials;
    std::vector<HttpRequest> requests;
    std::function<Json(const HttpRequest&)> answer;
    std::function<HttpResponse(const HttpRequest&)> rawAnswer;
    bool cancel = false;
    explicit Fixture(Provider p) : provider(p), credentials(Credentials(p)) {}
    Json Run(std::string_view action, Json args = Json::object()) {
        const auto before = credentials;
        if (!args.contains("_userId")) args["_userId"] = "77";
        auto result = Execute(provider, action, args, credentials, [&] { return cancel; },
            [&](const HttpRequest& r, const Cancelled&) {
                Expect(ValidHttpRequest(r), "request passes real fixed route policy");
                requests.push_back(r);
                if (rawAnswer) return rawAnswer(r);
                return Response(answer(r));
            });
        Expect(credentials == before, "borrowed credentials unchanged");
        return result;
    }
};
std::string LikedRawId(Provider provider, int index) {
    if (provider == Provider::Netease) return std::to_string(1000 + index);
    if (provider == Provider::QQ) return "liked" + std::to_string(index);
    auto id = HashHex("liked-" + std::to_string(index));
    std::transform(id.begin(), id.end(), id.begin(), [](char c) {
        return c >= 'a' && c <= 'f' ? static_cast<char>(c - 32) : c;
    });
    return id;
}
std::string LikedId(Provider provider, int index) {
    return std::string(Name(provider)) + ":" + LikedRawId(provider, index);
}
Json LikedTrack(Provider provider, int index) {
    auto track = provider == Provider::Netease ? TrackN(1000 + index) :
        provider == Provider::QQ ? TrackQ() : TrackK();
    if (provider == Provider::QQ) {
        track["mid"] = LikedRawId(provider, index);
        track["id"] = 1000 + index;
    } else if (provider == Provider::Kugou) track["FileHash"] = LikedRawId(provider, index);
    return track;
}
struct LikedFixture : Fixture {
    Json rows = Json::array(), library = Json::array();
    int snapshotCalls = 0, libraryCalls = 0;
    std::vector<int> snapshotOffsets;
    explicit LikedFixture(Provider p) : Fixture(p) {
        static int sequence = 0;
        const auto secret = "synthetic-liked-" + std::to_string(++sequence);
        credentials[Json::json_pointer(p == Provider::Netease ? "/MUSIC_U" :
            p == Provider::QQ ? "/credential/musicKey" : "/token")] = secret;
        if (p == Provider::QQ) {
            library = Json::array({
                {{"tid", 900}, {"dirid", 3}, {"diss_name", u8"\u6211\u559c\u6b22"}},
                {{"tid", 901}, {"dirid", 201}, {"diss_name", "Renamed built-in list"}}});
        } else if (p == Provider::Kugou) {
            library = Json::array({
                {{"global_collection_id", "ordinary"}, {"listid", 4}, {"type", 0},
                    {"name", u8"\u6211\u559c\u6b22"}},
                {{"global_collection_id", "likedGid"}, {"list_id", 2}, {"type", 0}, {"name", "Renamed built-in list"}},
                {{"global_collection_id", "subscribed"}, {"listid", 2}, {"type", 1}, {"name", "Subscribed"}},
                {{"musiclib_id", "22"}, {"source", 2}, {"listid", 2}, {"type", 0}, {"name", "Album"}}});
        }
        answer = [this](const HttpRequest& request) { return Reply(request); };
    }
    static Json Slice(const Json& source, int start, int count) {
        Json result = Json::array();
        for (std::size_t i = static_cast<std::size_t>(start);
            i < source.size() && result.size() < static_cast<std::size_t>(count); ++i) result.push_back(source[i]);
        return result;
    }
    bool IsSnapshot(const HttpRequest& r) const {
        if (provider == Provider::Netease) return r.url.find("/song/like/get") != r.url.npos;
        if (provider == Provider::QQ) {
            if (r.method != "POST") return false;
            const auto request = Json::parse(r.body)["req_0"];
            return request["method"] == "CgiGetDiss" && request["param"]["song_num"] == 100;
        }
        return r.url.find("/pubsongs/v2/get_other_list_file_nofilt") != r.url.npos &&
            Query(r.url).at("pagesize") == "100";
    }
    bool IsWrite(const HttpRequest& r) const {
        if (provider == Provider::QQ) {
            if (r.method != "POST") return false;
            const auto method = Json::parse(r.body)["req_0"]["method"];
            return method == "AddSonglist" || method == "DelSonglist";
        }
        return r.url.find("/radio/like") != r.url.npos || r.url.find("/add_song") != r.url.npos ||
            r.url.find("/delete_songs") != r.url.npos;
    }
    Json Reply(const HttpRequest& r) {
        const bool snapshot = IsSnapshot(r);
        if (snapshot) ++snapshotCalls;
        if (provider == Provider::Netease) {
            if (snapshot) {
                snapshotOffsets.push_back(0);
                Json ids = Json::array();
                for (const auto& row : rows) ids.push_back(row["id"]);
                return {{"code", 200}, {"ids", ids}};
            }
            if (r.url.find("/cloudsearch/pc") != r.url.npos)
                return {{"code", 200}, {"result", {{"songCount", 1},
                    {"songs", Json::array({LikedTrack(provider, 0)})}}}};
            if (r.url.find("/playlist/detail") != r.url.npos)
                return {{"code", 200}, {"playlist", {{"trackIds", Json::array({{{"id", 1000}}})}}}};
            if (r.url.find("/song/detail") != r.url.npos) {
                Json tracks = Json::array();
                for (const auto& id : Json::parse(Eapi(r)["c"].get<std::string>()))
                    tracks.push_back(TrackN(id["id"].get<int>()));
                return {{"code", 200}, {"songs", tracks}};
            }
            if (r.url.find("/play-record/") != r.url.npos)
                return {{"code", 200}, {"data", {{"list", Json::array({{{"data", LikedTrack(provider, 0)}}})}}}};
            if (r.url.find("/user/playlist") != r.url.npos)
                return {{"code", 200}, {"playlist", Json::array()}};
            if (r.url.find("/discovery/recommend/songs") != r.url.npos)
                return {{"code", 200}, {"data", {{"dailySongs", Json::array()}}}};
            if (r.url.find("/personalized/playlist") != r.url.npos)
                return {{"code", 200}, {"result", Json::array()}};
            if (r.url.find("/playlist/list") != r.url.npos)
                return {{"code", 200}, {"playlists", Json::array()}};
            Expect(IsWrite(r), "known NetEase liked fixture route");
            return {{"code", 200}};
        }
        if (provider == Provider::QQ) {
            if (r.method == "GET") {
                if (r.url.find("fcg_user_created_diss") != r.url.npos) {
                    ++libraryCalls;
                    return {{"code", 0}, {"data", {{"disslist",
                        Slice(library, std::stoi(Query(r.url).at("sin")), 100)}, {"total", library.size()}}}};
                }
                if (r.url.find("fcg_get_profile_order_asset") != r.url.npos)
                    return {{"code", 0}, {"data", {{"cdlist", Json::array({
                        {{"tid", 902}, {"dirid", 201}, {"diss_name", "Subscribed built-in list"}},
                        {{"tid", 900}, {"dirid", 0}, {"diss_name", "Subscribed duplicate"}}})}}}};
                Expect(r.url.find("fcg_get_diss_by_tag") != r.url.npos, "known QQ recommendations route");
                return {{"code", 0}, {"data", {{"list", Json::array()}}}};
            }
            const auto request = Json::parse(r.body)["req_0"];
            const auto params = request["param"];
            const auto method = request["method"];
            Json data;
            if (method == "DoSearchForQQMusicDesktop") {
                data = {{"body", {{"item_song", Json::array({LikedTrack(provider, 0)})}}}, {"meta", {{"sum", 1}}}};
            } else if (method == "CgiGetDiss") {
                const auto start = params["song_begin"].get<int>();
                if (snapshot) {
                    Expect(params["disstid"] == 901 && params["dirid"] == 0,
                        "QQ snapshot uses owned built-in list's actual dissId, not its name or subscribed list");
                    snapshotOffsets.push_back(start);
                } else if (params["disstid"] == 900 && libraryCalls) {
                    Expect(params["dirid"] == 0, "historical QQ bridge routes a nonzero dissId without dirId");
                }
                const bool liked = params["disstid"] == 901;
                data = {{"songlist", Slice(liked ? rows : Json::array({LikedTrack(provider, 0)}),
                    start, params["song_num"].get<int>())}, {"total_song_num", liked ? rows.size() : 1}};
            } else if (method == "get_song_detail_yqq") {
                const auto id = params["song_mid"].get<std::string>();
                data = {{"track_info", LikedTrack(provider, std::stoi(id.substr(5)))}};
            } else {
                Expect(IsWrite(r) && params["dirId"] == 201 && params["tid"] == 0,
                    "QQ likes use built-in write routing without a home visit");
                data = {{"retCode", 0}};
            }
            return {{"code", 0}, {"req_0", {{"code", 0}, {"data", data}}}};
        }
        if (r.url.find("/v7/get_all_list") != r.url.npos) {
            ++libraryCalls;
            const auto body = Json::parse(r.body);
            const auto count = body["pagesize"].get<int>();
            return {{"status", 1}, {"data", {{"info",
                Slice(library, (body["page"].get<int>() - 1) * count, count)}, {"total", library.size()}}}};
        }
        if (r.url.find("/pubsongs/v2/get_other_list_file_nofilt") != r.url.npos) {
            const auto query = Query(r.url);
            const auto start = std::stoi(query.at("begin_idx"));
            if (snapshot) {
                Expect(query.at("global_collection_id") == "likedGid",
                    "Kugou snapshot excludes title-only, subscribed and album matches");
                snapshotOffsets.push_back(start);
            }
            const bool liked = query.at("global_collection_id") == "likedGid";
            return {{"status", 1}, {"data", {{"info", Slice(liked ? rows :
                Json::array({LikedTrack(provider, 0)}), start, std::stoi(query.at("pagesize")))},
                {"total", liked ? rows.size() : 1}}}};
        }
        if (r.url.find("/v4/get_list_all_file") != r.url.npos)
            return {{"status", 1}, {"data", {{"info", Json::array({LikedTrack(provider, 0)})}}}};
        if (IsWrite(r)) {
            Expect(Json::parse(r.body)["listid"] == "2", "Kugou cached listId survives standalone likes");
            return {{"status", 1}};
        }
        if (r.url.find("/playhistory/") != r.url.npos)
            return {{"status", 1}, {"data", {{"list", Json::array({LikedTrack(provider, 0)})}, {"bp", ""}}}};
        if (r.url.find("/single_card_recommend") != r.url.npos)
            return {{"status", 1}, {"data", {{"info", Json::array()}}}};
        Expect(r.url.find("/search/song") != r.url.npos, "known Kugou liked fixture route");
        return {{"status", 1}, {"data", {{"info", Json::array({LikedTrack(provider, 0)})}, {"total", 1}}}};
    }
};
void QQLegacyCharset() {
    Fixture f(Provider::QQ);
    f.credentials = Json::object();
    f.rawAnswer = [](const HttpRequest& r) {
        Expect(r.method == "GET" && r.url.find("fcg_get_diss_by_tag") != r.url.npos,
            "QQ home uses the historical recommendation route");
        std::string body = R"({"code":0,"data":{"list":[{"dissid":"900","dissname":")";
        body.append({static_cast<char>(0xd6), static_cast<char>(0xd0),
            static_cast<char>(0xce), static_cast<char>(0xc4)});
        body += R"(","imgurl":"https://cover.test/radio.jpg"}],"sum":1}})";
        return HttpResponse{200, std::move(body), "application/x-javascript;charset=gb2312", {}, {}};
    };
    const auto page = f.Run("home");
    Expect(page["recommendations"].size() == 1 &&
        page["recommendations"][0]["title"] == std::string("\xe4\xb8\xad\xe6\x96\x87", 6),
        "QQ legacy GB2312 recommendation is decoded as UTF-8");
}
void Netease() {
    Fixture f(Provider::Netease);
    f.answer = [](const HttpRequest& r) {
        if (r.url.find("/eapi/cloudsearch/pc") != r.url.npos) {
            const auto p = Eapi(r);
            Expect(p["s"] == "test" && p["limit"] == 50 && p["offset"] == 0 && p["type"] == 1,
                "historical search parameters/default limit");
            return Json{{"code", 200}, {"result", {{"songs", Json::array({TrackN()})}, {"songCount", 80}}}};
        }
        if (r.url.find("/eapi/v6/playlist/detail") != r.url.npos) {
            Expect(Eapi(r)["n"] == 0, "playlist detail does not fetch full songs");
            return Json{{"code", 200}, {"playlist", {{"trackIds", Json::array({{{"id", 41}}, {{"id", 42}}, {{"id", 43}}})}}}};
        }
        if (r.url.find("/eapi/v3/song/detail") != r.url.npos) {
            const auto ids = Json::parse(Eapi(r)["c"].get<std::string>());
            Expect(ids.size() == 1 && ids[0]["id"] == 42, "slice IDs before song request");
            return Json{{"code", 200}, {"songs", Json::array({TrackN()})}};
        }
        if (r.url.find("/eapi/song/like/get") != r.url.npos)
            return Json{{"code", 200}, {"ids", Json::array({41, 42, 43})}};
        if (r.url.find("/weapi/play-record/song/list") != r.url.npos)
            return Json{{"code", 200}, {"data", {{"list", Json::array({{{"data", TrackN()}}})}}}};
        if (r.url.find("/eapi/song/enhance/player/url") != r.url.npos) {
            const auto p = Eapi(r);
            Expect(r.url.find("/url/v1") != r.url.npos && p["level"] == "standard" &&
                p["encodeType"] == "flac" && p["ids"] == "[\"42\"]", "authorized level-based song-url");
            return Json{{"code", 200}, {"data", Json::array({{{"id", 42}, {"url", "http://m7.music.126.net/authorized.mp3"},
                {"freeTrialInfo", nullptr}, {"code", 200}}})}};
        }
        if (r.url.find("/eapi/song/lyric/v1") != r.url.npos)
            return Json{{"code", 200}, {"yrc", {{"lyric", "[0,1000](0,1000,0)fixture"}}}};
        if (r.url.find("/weapi/v3/discovery/recommend/songs") != r.url.npos) {
            auto free = TrackN(43);
            free["fee"] = 8;
            return Json{{"code", 200}, {"data", {{"dailySongs", Json::array({TrackN(), free})}}}};
        }
        if (r.url.find("/weapi/personalized/playlist") != r.url.npos)
            return Json{{"code", 200}, {"result", Json::array({{{"id", 9}, {"name", "Recommended"}}})}};
        if (r.url.find("/weapi/user/playlist") != r.url.npos)
            return Json{{"code", 200}, {"playlist", Json::array({{{"id", 9}, {"name", "Owned"},
                {"creator", {{"userId", 77}}}}})}, {"more", true}};
        if (r.url.find("/weapi/playlist/list") != r.url.npos)
            return Json{{"code", 200}, {"playlists", Json::array({{{"id", 9}, {"name", "Explore"}}})}, {"more", false}};
        if (r.url.find("/weapi/playlist/create") != r.url.npos)
            return Json{{"code", 200}, {"playlist", {{"id", 10}, {"name", "New"}}}};
        if (r.url.find("/eapi/playlist/manipulate/tracks") != r.url.npos) {
            const auto p = Eapi(r);
            Expect(p["pid"] == "9" && p["trackIds"] == "[\"42\"]" && p["op"] == "add",
                "native IDs stripped for playlist add");
            return Json{{"code", 200}};
        }
        Expect(r.url.find("/weapi/radio/like") != r.url.npos, "known Netease fixture route");
        return Json{{"code", 200}};
    };
    auto page = f.Run("search", {{"query", "test"}});
    TrackSchema(page, f.provider);
    Expect(page["tracks"][0]["vipRequired"] == true, "Netease paid track is marked");
    Expect(page["more"] == true && page["tracks"][0]["duration"] == 123.456, "Netease search more/seconds");
    TrackSchema(f.Run("playlist", {{"id", "9"}, {"offset", 1}, {"limit", 1}}), f.provider);
    TrackSchema(f.Run("favorites", {{"offset", 1}, {"limit", 1}}), f.provider);
    TrackSchema(f.Run("recent"), f.provider);
    auto home = f.Run("home");
    CollectionSchema(home);
    Expect(home["playlists"][0]["owned"] == true, "NetEase ownership uses lease account");
    Expect(home["tracks"].size() == 2 && home["tracks"][0]["membershipBadge"] == "netease-vip" &&
        !home["tracks"][1].contains("membershipBadge") && home["tracks"][1]["vipRequired"] == false,
        "NetEase home lists daily songs; fee 8 is free at standard quality, fee 1 needs 黑胶VIP");
    CollectionSchema(f.Run("explore"));
    CollectionSchema(f.Run("createPlaylist", {{"title", "New"}}));
    Expect(f.Run("addToPlaylist", {{"id", "9"}, {"trackId", "netease:42"}})["ok"] == true, "Netease add");
    Expect(f.Run("like", {{"id", "netease:42"}, {"value", false}})["liked"] == false, "Netease unlike");
    Expect(f.Run("resolve", {{"id", "42"}})["url"] == "https://m7.music.126.net/authorized.mp3", "Netease HTTPS upgrade");
    Expect(f.Run("lyrics", {{"id", "42"}}).contains("yrc"), "Netease raw lyrics");
    Error([&] { f.Run("recent", {{"offset", 100}}); }, "unsupported");
}
void QQ() {
    Fixture f(Provider::QQ);
    int likedPageCalls = 0;
    f.answer = [&](const HttpRequest& r) {
        if (r.method == "GET") {
            if (r.url.find("fcg_get_diss_by_tag") != r.url.npos)
                return Json{{"code", 0}, {"data", {{"list", Json::array({{{"dissid", "900"}, {"dissname", "Recommend"}}})}}}};
            if (r.url.find("fcg_user_created_diss") != r.url.npos)
                return Json{{"code", 0}, {"data", {{"disslist", Json::array({
                    {{"tid", 900}, {"dirid", 3}, {"diss_name", "Owned"}},
                    {{"tid", 0}, {"dirid", 201}, {"diss_name", "QQ favorites"}}})}}}};
            return Json{{"code", 0}, {"data", {{"cdlist", Json::array()}}}};
        }
        const auto p = Json::parse(r.body);
        const auto key = p.contains("request") ? "request" : "req_0";
        const auto method = p[key]["method"].get<std::string>();
        const auto param = p[key]["param"];
        Json data;
        if (method == "DoSearchForQQMusicDesktop") {
            Expect(param["num_per_page"] == 50 && param["query"] == "test", "QQ search contract");
            data = {{"body", {{"item_song", Json::array({TrackQ()})}}}, {"meta", {{"sum", 80}}}};
        } else if (method == "CgiGetDiss") {
            Expect(param["song_num"] == 50 || (param["dirid"] == 201 &&
                (param["song_num"] == 100 || param["song_num"] == 1)),
                "QQ bounded playlist and read-only liked snapshot");
            if (param["dirid"] == 201 && param["song_num"] == 100) ++likedPageCalls;
            data = {{"songlist", Json::array({TrackQ()})}, {"total_song_num", 1}};
        } else if (method == "CreateFolder") data = {{"tid", 901}};
        else if (method == "AddSonglist" || method == "DelSonglist") {
            Expect(param["v_songInfo"][0]["songId"] == 123, "QQ numeric song metadata stays native");
            data = {{"retCode", 0}};
        } else if (method == "CgiGetVkey") {
            Expect(param["filename"][0] == "C400mediaMID.m4a" && param["songmid"][0] == "songMID",
                "QQ mediaMID differs from stable songMID");
            Expect(p["comm"]["ct"] == 19 && p["comm"]["uid"] == "uid", "QQ authorized mobile session");
            data = {{"retcode", 0}, {"sip", Json::array({"http://isure.stream.qqmusic.qq.com/"})},
                {"midurlinfo", Json::array({{{"result", 0}, {"purl", "authorized.m4a?vkey=fixture"}}})}};
        } else if (method == "GetPlayLyricInfo") {
            Expect(param == Json{{"songMID", "songMID"}} && key == std::string("request") &&
                p["comm"] == Json{{"ct", 24}, {"cv", 0}, {"format", "json"}, {"uin", 0}} &&
                r.headers.at("cookie").empty(),
                "QQ historical public LRC request");
            data = {{"crypt", 0}, {"lyric", lume::music::auth::Base64(lineLyrics)}};
        } else {
            Expect(method == "get_song_detail_yqq", "known QQ fixture method");
            data = {{"track_info", TrackQ()}};
        }
        return Json{{"code", 0}, {key, {{"code", 0}, {"data", data}}}};
    };
    auto page = f.Run("search", {{"query", "test"}});
    TrackSchema(page, f.provider);
    Expect(page["tracks"][0]["vipRequired"] == true, "QQ paid track is marked");
    Expect(page["likedIds"] == Json::array({"qq:songMID"}) && likedPageCalls == 1,
        "QQ search carries one complete liked snapshot");
    Expect(f.Run("search", {{"query", "test"}, {"offset", 50}})["likedIds"] ==
        Json::array({"qq:songMID"}) && likedPageCalls == 1,
        "QQ appended search reuses the complete liked snapshot");
    TrackSchema(f.Run("playlist", {{"id", "900"}}), f.provider);
    TrackSchema(f.Run("favorites"), f.provider);
    const auto home = f.Run("home");
    CollectionSchema(home);
    Expect(home["likedIds"] == Json::array({"qq:songMID"}), "QQ home loads liked IDs read-only");
    CollectionSchema(f.Run("explore"));
    Expect(f.Run("createPlaylist", {{"title", "New"}})["ok"] == true, "QQ create");
    Expect(f.Run("like", {{"id", "qq:songMID"}, {"value", true}})["liked"] == true, "QQ like");
    Expect(f.Run("like", {{"id", "qq:songMID"}, {"value", false}})["liked"] == false, "QQ unlike");
    // Explore must not erase the already established account-owned routing metadata.
    Expect(f.Run("addToPlaylist", {{"id", "900"}, {"trackId", "qq:songMID"}})["ok"] == true, "QQ add");
    Expect(f.Run("resolve", {{"id", "songMID"}})["url"].get<std::string>().find("https://") == 0, "QQ HTTPS resolve");
    const auto parsed = ParseOnlineLyrics(f.provider, f.Run("lyrics", {{"id", "songMID"}}), 123);
    Expect(parsed.error.empty() && parsed.lines.size() == 2 && parsed.lines[0].startTime == 1.25 &&
        parsed.lines[1].startTime == 3.75 && parsed.lines[0].words.empty(), "QQ returns real line-timed LRC");
    Error([&] { f.Run("recent"); }, "unsupported");
}
void Kugou() {
    Fixture f(Provider::Kugou);
    f.answer = [](const HttpRequest& r) {
        auto params = Query(r.url);
        const auto signature = params.at("signature");
        params.erase("signature");
        const std::string salt = "LnT6xpN3khm36zse0QzvmgTZ3waWdRSA";
        std::string signedText = salt;
        for (const auto& [key, value] : params) signedText += key + "=" + value;
        Expect(signature == HashHex(signedText + r.body + salt), "Kugou historical signature exact");
        if (r.url.find("lyrics.kugou.com/v1/search") != r.url.npos) {
            Expect(!params.count("token") && params["appid"] == "1005" && params["duration"] == "0",
                "Kugou lyric search clears default parameters");
            Expect(r.headers.at("dfid") == "synthetic-dfid" && r.headers.at("mid") == "123456" &&
                r.headers.at("kg-rc") == "1" && !r.headers.count("clienttime"), "historical lyric headers");
            return Json{{"status", 200}, {"candidates", Json::array({{{"id", "55"}, {"accesskey", "fixture-key"}}})}};
        }
        if (r.url.find("lyrics.kugou.com/download") != r.url.npos) {
            Expect(params["fmt"] == "lrc" && params["accesskey"] == "fixture-key", "Kugou historical LRC fallback");
            return Json{{"status", 200}, {"content", lume::music::auth::Base64(lineLyrics)}, {"contenttype", 1}};
        }
        if (r.url.find("/v5/url") != r.url.npos) {
            Expect(r.headers.at("x-router") == "trackercdn.kugou.com" && params["IsFreePart"] == "0", "Kugou full authorized playback");
            Expect(params["key"] == HashHex(std::string(32, 'a') +
                "185672dd44712f60bb1736df5a377e82" + "3116" + "123456" + "77"), "Kugou play-key digest");
            return Json{{"status", 1}, {"url", Json::array({"http://fs.w.kugou.com/authorized.mp3"})}};
        }
        if (r.url.find("/v7/get_all_list") != r.url.npos)
            return Json{{"status", 1}, {"data", {{"total", 2}, {"info", Json::array({
                {{"global_collection_id", "likedGid"}, {"listid", 2}, {"name", "Liked"}, {"type", 0}},
                {{"global_collection_id", "ownedGid"}, {"listid", 4}, {"name", "Owned"}, {"type", 0}}})}}}};
        if (r.url.find("/v4/get_list_all_file") != r.url.npos) {
            auto row = TrackK(); row["fileid"] = 567;
            return Json{{"status", 1}, {"data", {{"info", Json::array({row})}}}};
        }
        if (r.url.find("/v4/delete_songs") != r.url.npos) {
            Expect(Json::parse(r.body)["data"][0]["fileid"] == 567, "unlike uses favorite-list file ID, not search file ID");
            return Json{{"status", 1}};
        }
        if (r.url.find("/add_song") != r.url.npos) {
            const auto p = Json::parse(r.body);
            Expect(p["data"][0]["hash"] == hash && p["data"][0]["album_id"] == 22, "Kugou native add metadata");
            return Json{{"status", 1}};
        }
        if (r.url.find("/add_list") != r.url.npos) return Json{{"status", 1}};
        if (r.url.find("/playhistory/v1/get_songs") != r.url.npos) {
            const auto p = Json::parse(r.body);
            Expect(p["userid"] == "77" && p["source_classify"] == "app" &&
                p["to_subdivide_sr"] == 1 && !p.contains("bp"), "Kugou history initial request");
            return Json{{"status", 1}, {"data", {{"list", Json::array({TrackK()})}, {"bp", ""}}}};
        }
        if (r.url.find("/search/") == r.url.npos)
            return Json{{"status", 1}, {"data", {{"total", 1}, {"info", Json::array({TrackK()})}}}};
        // Live search spelling (2026-10-11): PayType and Privilege.
        auto member = TrackK(), free = TrackK();
        member.erase("pay_type"); member["FileHash"] = "B3A52A7A958BF0AED0EBFBA2E9A818B7";
        member["PayType"] = 3; member["Privilege"] = 10;
        free["FileHash"] = "37A8F50A9EC3B267C3CC6BEC633D9C4A"; free["Privilege"] = 8;
        return Json{{"status", 1}, {"data", {{"total", 3}, {"info", Json::array({TrackK(), member, free})}}}};
    };
    auto page = f.Run("search", {{"query", "test"}});
    TrackSchema(page, f.provider);
    Expect(page["tracks"][0]["vipRequired"] == true, "Kugou paid track is marked");
    Expect(page["tracks"].size() == 3 && page["tracks"][1]["membershipBadge"] == "kugou-vip" &&
        page["tracks"][2]["vipRequired"] == false && !page["tracks"][2].contains("membershipBadge"),
        "KuGou privilege 10 needs VIP and privilege 8 is free whatever PayType says");
    TrackSchema(f.Run("playlist", {{"id", "publicGid"}}), f.provider);
    TrackSchema(f.Run("favorites"), f.provider);
    const auto home = f.Run("home");
    CollectionSchema(home);
    Expect(home["likedIds"] == Json::array({"kugou:" + hash}), "Kugou home loads liked IDs read-only");
    CollectionSchema(f.Run("explore"));
    Expect(f.Run("createPlaylist", {{"title", "New"}})["ok"] == true, "Kugou create");
    Expect(f.Run("like", {{"id", "kugou:" + hash}, {"value", true}})["liked"] == true, "Kugou like");
    Expect(f.Run("like", {{"id", hash}, {"value", false}})["liked"] == false, "Kugou unlike");
    Expect(f.Run("addToPlaylist", {{"id", "ownedGid"}, {"trackId", "kugou:" + hash}})["ok"] == true, "Kugou add");
    Expect(f.Run("resolve", {{"id", hash}})["url"] == "http://fs.w.kugou.com/authorized.mp3",
        "Kugou native playback preserves the authorized CDN protocol");
    const auto lyrics = f.Run("lyrics", {{"id", hash}});
    Expect(lyrics["content"] == lume::music::auth::Base64(lineLyrics) && !lyrics.contains("decodeContent"),
        "Kugou preserves actual Base64 LRC payload");
    const auto parsed = ParseOnlineLyrics(f.provider, lyrics, 123);
    Expect(parsed.error.empty() && parsed.lines.size() == 2 &&
        parsed.lines[0].startTime == 1.25 && parsed.lines[1].startTime == 3.75 &&
        parsed.lines[0].words.empty() && parsed.lines[1].words.empty(),
        "Kugou fallback reaches existing parser with real line times and no invented words");
    TrackSchema(f.Run("recent"), f.provider);
}
void LyricsFallback() {
    for (auto provider : {Provider::QQ, Provider::Kugou}) {
        Fixture f(provider);
        Json payload;
        f.answer = [&](const HttpRequest& r) -> Json {
            if (provider == Provider::QQ) {
                const auto request = Json::parse(r.body);
                if (request.contains("req_0"))
                    return {{"code", 0}, {"req_0", {{"code", 0}, {"data", {{"track_info", TrackQ()}}}}}};
                const auto params = request["request"]["param"];
                Expect(params == Json{{"songMID", "songMID"}},
                    "QQ public LRC uses songMID only, no guessed crypt/qrc flags");
                return {{"code", 0}, {"request", {{"code", 0}, {"data", payload}}}};
            }
            if (r.url.find("/v1/search") != r.url.npos)
                return {{"status", 200}, {"candidates", Json::array({{{"id", "55"}, {"accesskey", "fixture-key"}}})}};
            Expect(Query(r.url).at("fmt") == "lrc", "all Kugou lyric downloads use supported LRC");
            return payload;
        };
        const Json args = {{"id", provider == Provider::QQ ? "songMID" : hash}};
        const auto valid = provider == Provider::QQ ?
            Json{{"crypt", 0}, {"lyric", lume::music::auth::Base64(lineLyrics)}} :
            Json{{"status", 200}, {"content", lume::music::auth::Base64(lineLyrics)}, {"contenttype", 1}};
        payload = valid;
        const auto response = f.Run("lyrics", args);
        const auto parsed = ParseOnlineLyrics(provider, response, 123);
        Expect(parsed.error.empty() && parsed.lines.size() == 2 &&
            parsed.lines[0].startTime == 1.25 && parsed.lines[1].startTime == 3.75 &&
            parsed.lines[0].words.empty() && parsed.lines[1].words.empty(),
            "provider-to-core line timing contract");
        payload = provider == Provider::QQ ? Json{{"crypt", 1}, {"lyric", lineLyrics}} :
            Json{{"status", 200}, {"content", lume::music::auth::Base64("krc1compressed-fixture")}, {"contenttype", 0}};
        Error([&] { f.Run("lyrics", args); }, "unsupported");
        payload = valid;
        const auto* field = provider == Provider::QQ ? "lyric" : "content";
        payload[field] = "";
        Error([&] { f.Run("lyrics", args); }, "unavailable");
        payload[field] = "@@not-base64@@";
        Error([&] { f.Run("lyrics", args); }, "invalid-response");
        payload[field] = lume::music::auth::Base64("[ar:Fixture]\nNo time markers");
        Error([&] { f.Run("lyrics", args); }, "unavailable");
    }
}
void QQPublicLyrics() {
    int calls = 0;
    const Exchange transport = [&](const HttpRequest& request, const Cancelled&) {
        ++calls;
        Expect(ValidHttpRequest(request) && request.url == "https://u.y.qq.com/cgi-bin/musicu.fcg",
            "QQ LRC keeps the existing fixed native route");
        const auto body = Json::parse(request.body);
        Expect(body.size() == 2 && body["request"]["method"] == "GetPlayLyricInfo" &&
            body["request"]["param"] == Json{{"songMID", "freshMID"}} &&
            body["comm"] == Json{{"cv", 0}, {"ct", 24}, {"format", "json"}, {"uin", 0}} &&
            request.headers.at("cookie").empty(), "cold anonymous lyrics need no credentials or detail lookup");
        return Response({{"code", 0}, {"request", {{"code", 0}, {"data", {{"crypt", 0},
            {"lyric", lume::music::auth::Base64(lineLyrics)},
            {"trans", lume::music::auth::Base64("[00:01.250]Translation\n[00:03.750]Second translation")}}}}}});
    };
    for (const auto* id : {"freshMID", "qq:freshMID"}) {
        const auto credentials = std::string_view(id) == "freshMID" ? Json::object() : Credentials(Provider::QQ);
        const auto raw = Execute(Provider::QQ, "lyrics", {{"id", id}}, credentials, {}, transport);
        const auto parsed = ParseOnlineLyrics(Provider::QQ, raw, 20);
        Expect(parsed.error.empty() && parsed.lines.size() == 2 &&
            parsed.lines[0].startTime == 1.25 && parsed.lines[1].startTime == 3.75 &&
            parsed.lines[0].translation == "Translation" && parsed.lines[0].words.empty() &&
            parsed.lines[1].words.empty(), "anonymous LRC preserves real times and translations");
    }
    Expect(calls == 2, "exactly one native request per QQ lyrics action");
    Error([&] { Execute(Provider::QQ, "lyrics", {{"id", "kugou:freshMID"}}, Json::object(), {}, transport); },
        "invalid-response");
    Expect(calls == 2, "cross-provider lyric ID rejected before transport");
}
void QQCollections() {
    Fixture f(Provider::QQ);
    f.credentials["credential"]["musicKey"] = "synthetic-qq-collections";
    Json created = Json::array({{{"tid", 0}, {"dirid", 205}, {"diss_name", "Not a playlist"}}});
    for (int i = 0; i < 105; ++i)
        created.push_back({{"tid", 9000 + i}, {"dirid", i + 3}, {"diss_name", "Owned"}});
    const Json collected = Json::array({
        {{"dissid", "9000"}, {"dissname", "Duplicate"}},
        {{"dissid", "10000"}, {"dissname", "Collected A"}},
        {{"dissid", "10001"}, {"dissname", "Collected B"}},
        {{"dissid", "10002"}, {"dissname", "Collected C"}}});
    f.answer = [&](const HttpRequest& r) -> Json {
        if (r.method == "POST")
            return {{"code", 0}, {"req_0", {{"code", 0},
                {"data", {{"songlist", Json::array()}, {"total_song_num", 0}}}}}};
        const auto params = Query(r.url);
        if (r.url.find("fcg_get_diss_by_tag") != r.url.npos)
            return {{"code", 0}, {"data", {{"list", Json::array()}, {"sum", 0}}}};
        const bool owned = r.url.find("fcg_user_created_diss") != r.url.npos;
        if (!owned) Expect(r.url.find("fcg_get_profile_order_asset.fcg") != r.url.npos &&
            params.at("userid") == "77" && params.at("reqtype") == "3" &&
            params.at("cid") == "205360956" && params.at("ct") == "20", "QQ historical collection request");
        const int start = std::stoi(params.at("sin"));
        Expect(owned ? params.at("size") == "100" :
            std::stoi(params.at("ein")) == start + 99, "QQ independent source pagination");
        const auto& source = owned ? created : collected;
        Json rows = Json::array();
        for (int i = start; i < static_cast<int>(source.size()) && i < start + 100; ++i) rows.push_back(source[i]);
        return {{"code", 0}, {"data", {{owned ? "disslist" : "cdlist", rows},
            {owned ? "total" : "totalnum", source.size()}}}};
    };
    const auto first = f.Run("home", {{"limit", 100}});
    const auto next = f.Run("home", {{"offset", 100}, {"limit", 100}});
    CollectionSchema(first); CollectionSchema(next);
    Expect(first["playlists"].size() == 108 && first["playlists"][0]["id"] == "9000" &&
        first["playlists"][0]["owned"] == true && first["more"] == false, "QQ owned wins duplicate");
    Expect(first["playlists"][105]["id"] == "10000" && first["playlists"][105]["owned"] == false,
        "QQ whole library arrives with the first home page, with no dropped tail");
    Expect(next["playlists"].empty(), "QQ later home pages do not repeat the library");
    const auto answer = f.answer;
    f.answer = [&](const HttpRequest& r) {
        if (r.url.find("fcg_get_profile_order_asset") != r.url.npos) return Json{{"code", 1000}};
        return answer(r);
    };
    const auto degraded = f.Run("home");
    CollectionSchema(degraded);
    Expect(!degraded["playlists"].empty() && degraded["playlists"][0]["owned"] == true,
        "QQ home keeps owned playlists when subscribed source expires");
    f.answer = [&](const HttpRequest& r) {
        if (r.url.find("fcg_get_profile_order_asset") != r.url.npos) return Json{{"code", 0}, {"data", Json::object()}};
        return answer(r);
    };
    const auto malformed = f.Run("home");
    CollectionSchema(malformed);
    Expect(!malformed["playlists"].empty() && malformed["playlists"][0]["owned"] == true,
        "QQ home keeps owned playlists when subscribed source is malformed");
    f.answer = [&](const HttpRequest& r) {
        auto response = answer(r);
        if (r.url.find("fcg_user_created_diss") != r.url.npos) {
            response["data"]["disslist"] = Json::array({created[1]});
            response["data"]["total"] = 1001;
        }
        return response;
    };
    const auto bounded = f.Run("home");
    Expect(!bounded["playlists"].empty() && bounded["playlists"][0]["owned"] == true,
        "QQ library scan beyond 1000 rows keeps what it read instead of failing home");
}
// 1.0.3.3 regressions: line-broken descriptions, failed recommendations,
// malformed rows, >1 MiB NetEase playlists and withheld liked songs.
void CatalogResilience() {
    Fixture n(Provider::Netease);
    int libraryPage = 0;
    Json bigIds = Json::array();
    for (int i = 0; i < 10000; ++i)
        bigIds.push_back({{"id", 100000 + i}, {"v", 1}, {"rcmdReason", std::string(120, 'x')}});
    n.answer = [&](const HttpRequest& r) -> Json {
        if (r.url.find("/weapi/personalized/playlist") != r.url.npos) return Json{{"code", 500}};
        if (r.url.find("/weapi/user/playlist") != r.url.npos) {
            Json list = Json::array();
            const int count = libraryPage++ == 0 ? 50 : 2;
            for (int i = 0; i < count; ++i)
                list.push_back({{"id", 500 + libraryPage * 100 + i}, {"name", "Owned\nlist"},
                    {"description", "line one\nline two\t" + std::string(3000, 'd')}, {"creator", {{"userId", 77}}}});
            list.push_back({{"id", 499}, {"description", "missing name"}});
            return Json{{"code", 200}, {"playlist", list}, {"more", libraryPage == 1}};
        }
        if (r.url.find("/weapi/v3/discovery/recommend/songs") != r.url.npos) return Json{{"code", 200}, {"data", {{"dailySongs", Json::array()}}}};
        if (r.url.find("/eapi/v6/playlist/detail") != r.url.npos)
            return Json{{"code", 200}, {"playlist", {{"trackIds", bigIds}}}};
        if (r.url.find("/eapi/v3/song/detail") != r.url.npos) {
            auto broken = TrackN(100001);
            broken.erase("name");
            auto titled = TrackN(100002);
            titled["name"] = "Two\nlines";
            return Json{{"code", 200}, {"songs", Json::array({TrackN(100000), broken, titled})}};
        }
        if (r.url.find("/eapi/song/like/get") != r.url.npos)
            return Json{{"code", 200}, {"ids", Json::array({41, "not-a-number", 42})}};
        return Json{{"code", 200}};
    };
    const auto home = n.Run("home");
    CollectionSchema(home);
    Expect(home["playlists"].size() == 52 && home["more"] == false,
        "NetEase home delivers every account playlist even when recommendations fail");
    for (const auto& item : home["playlists"])
        Expect(item["title"] == "Owned list" && item["description"].get<std::string>().find_first_of("\n\t") ==
            std::string::npos && item["description"].get<std::string>().size() <= 2000,
            "collection text is one bounded line");
    Expect(home.contains("likedIds") && home["likedIds"].size() == 2, "malformed liked ID skipped");
    const auto large = n.Run("playlist", {{"id", "9"}});
    Expect(large["tracks"].size() == 2 && large["tracks"][1]["title"] == "Two lines" && large["more"] == true,
        "a >1 MiB NetEase playlist opens and one malformed song does not hide the page");

    Fixture q(Provider::QQ);
    q.credentials["credential"]["musicKey"] = "synthetic-qq-withheld";
    q.answer = [&](const HttpRequest& r) -> Json {
        if (r.method == "GET") {
            if (r.url.find("fcg_user_created_diss") != r.url.npos)
                return Json{{"code", 0}, {"data", {{"disslist", Json::array({
                    {{"tid", 0}, {"dirid", 201}, {"diss_name", "QQ favorites"}}})}, {"total", 1}}}};
            return Json{{"code", 0}, {"data", {{"cdlist", Json::array()}, {"totalnum", 0}}}};
        }
        const auto param = Json::parse(r.body)["req_0"]["param"];
        auto withheld = TrackQ();
        withheld["mid"] = "secondMID";
        // total counts a song QQ withholds, so the scan never reaches it.
        const auto rows = param["song_begin"] == 0 ? Json::array({TrackQ(), withheld}) : Json::array();
        return {{"code", 0}, {"req_0", {{"code", 0}, {"data", {{"songlist", rows}, {"total_song_num", 3}}}}}};
    };
    const auto likes = q.Run("likes");
    Expect(likes["likedIds"].size() == 2, "QQ liked snapshot survives a withheld song");
}
void QQEmptyLibraryIsAnError() {
    Fixture f(Provider::QQ);
    f.credentials["credential"]["musicKey"] = "synthetic-qq-library-failure";
    f.answer = [](const HttpRequest& r) -> Json {
        if (r.method == "GET") {
            if (r.url.find("fcg_get_diss_by_tag") != r.url.npos)
                return Json{{"code", 0}, {"data", {{"list", Json::array()}}}};
            return Json{{"code", 1000}};
        }
        return Json{{"code", 0}, {"req_0", {{"code", 0}, {"data", Json::object()}}}};
    };
    // An empty private library after every source failed must not look like
    // "this account has no playlists"; the page must be able to show a retry.
    Error([&] { f.Run("home"); }, "expired");
}
void QQPartialLibraryKeepsCollectedLists() {
    Fixture f(Provider::QQ);
    f.credentials["credential"]["musicKey"] = "synthetic-qq-library-partial";
    f.answer = [](const HttpRequest& r) -> Json {
        if (r.method == "GET") {
            if (r.url.find("fcg_get_diss_by_tag") != r.url.npos)
                return Json{{"code", 0}, {"data", {{"list", Json::array()}}}};
            if (r.url.find("fcg_user_created_diss") != r.url.npos)
                return Json{{"code", 1000}};
            return Json{{"code", 0}, {"data", {{"cdlist", Json::array({
                {{"tid", 902}, {"dirid", 0}, {"diss_name", "Collected"}}})}}}};
        }
        return Json{{"code", 0}, {"req_0", {{"code", 0}, {"data", Json::object()}}}};
    };
    const auto home = f.Run("home");
    CollectionSchema(home);
    Expect(home["playlists"].size() == 1 && home["playlists"][0]["title"] == "Collected" &&
        home["playlists"][0]["owned"] == false, "a failed private source keeps the collected lists");
}
void KugouShortPagesContinue() {
    Fixture f(Provider::Kugou);
    f.credentials["token"] = "synthetic-kugou-short-pages";
    std::vector<int> pages, sizes;
    f.answer = [&](const HttpRequest& r) -> Json {
        if (r.url.find("/v7/get_all_list") != r.url.npos) {
            const auto body = Json::parse(r.body);
            const auto page = body["page"].get<int>();
            const auto size = body["pagesize"].get<int>();
            pages.push_back(page);
            sizes.push_back(size);
            // The gateway caps every page at 30 even when 100 is requested.
            const int start = (page - 1) * 30;
            const int count = std::clamp(150 - start, 0, 30);
            Json items = Json::array();
            for (int i = 0; i < count; ++i) {
                const auto index = start + i;
                items.push_back({{"global_collection_id", "list" + std::to_string(index)},
                    {"listid", index + 10}, {"type", 0}, {"name", "List " + std::to_string(index)}});
            }
            return Json{{"status", 1}, {"data", {{"info", items}, {"total", 150}}}};
        }
        return Json{{"status", 0}, {"error_code", 1}};
    };
    const auto home = f.Run("home");
    CollectionSchema(home);
    // A gateway that caps pages below the requested 100 must advance by the
    // rows it actually returned; 150 playlists at 30 per page need five pages.
    Expect(home["playlists"].size() == 150, "short KuGou pages load the whole library");
    Expect(pages.size() >= 5 && sizes.size() >= 5 && sizes[0] == 100 && sizes[1] == 30 &&
        sizes[2] == 30 && sizes[3] == 30 && sizes[4] == 30,
        "the first page discovers the 30-row cap and later pages use it");
    for (int page = 1; page <= 5; ++page)
        Expect(std::find(pages.begin(), pages.end(), page) != pages.end(),
            "every KuGou library page is requested exactly by number");
}
void QQHomeAndLikedCollection() {
    Fixture f(Provider::QQ);
    f.credentials["credential"]["musicKey"] = "synthetic-qq-home-parity";
    Json recommendations = Json::array();
    for (const auto* field : {"pic", "picUrl", "coverUrl", "cover_url", "imgurl"}) {
        const auto id = std::to_string(700 + recommendations.size());
        recommendations.push_back({{"dissid", id}, {"dissname", "Recommendation " + id},
            {field, "//qpic.y.qq.com/music_cover/" + id + "/600"}});
    }
    recommendations.push_back({{"dissid", "705"}, {"dissname", "Album artwork"}, {"diss_cover_mid", "albumMID"}});
    int coverReads = 0, homeReads = 0;
    f.answer = [&](const HttpRequest& r) -> Json {
        if (r.method == "GET") {
            if (r.url.find("fcg_get_diss_by_tag") != r.url.npos) {
                const int start = std::stoi(Query(r.url).at("sin"));
                return {{"code", 0}, {"data", {{"list", start == 0 ? recommendations : Json::array()},
                    {"sum", recommendations.size()}}}};
            }
            if (r.url.find("fcg_user_created_diss") != r.url.npos)
                return {{"code", 0}, {"data", {{"disslist", Json::array({
                    {{"tid", 901}, {"dirid", 201}, {"diss_name", "Renamed favorites"},
                        {"pic", "http://qpic.y.qq.com/music_cover/placeholder/600"}},
                    {{"tid", 902}, {"dirid", 3}, {"diss_name", u8"\u6211\u559c\u6b22"}}})}}}};
            return {{"code", 0}, {"data", {{"cdlist", Json::array({
                {{"tid", 903}, {"dirid", 201}, {"diss_name", "Another user's favorites"}}})}}}};
        }
        const auto request = Json::parse(r.body)["req_0"];
        Expect(request["method"] == "CgiGetDiss", "QQ home uses historical collection tracks, never radio or search");
        const auto params = request["param"];
        const auto diss = params["disstid"].get<int>();
        Expect(params["dirid"] == 0, "QQ historical bridge omits dirId when the real dissId is nonzero");
        if (params["song_num"] == 100)
            return {{"code", 0}, {"req_0", {{"code", 500}, {"data", Json::object()}}}};
        auto song = TrackQ();
        if (diss == 901) {
            song["mid"] = "favoriteMID";
            song["coverUrl"] = "http://qpic.y.qq.com/music_cover/newest/600";
            if (params["song_num"] == 1) {
                Expect(params["song_begin"] == 0, "liked cover is always the first song");
                ++coverReads;
            }
        } else {
            Expect(diss == 700, "home follows the focused first recommendation, not another library playlist");
            ++homeReads;
            song["mid"] = "home" + params["song_begin"].dump();
        }
        return {{"code", 0}, {"req_0", {{"code", 0}, {"data", {
            {"songlist", Json::array({song})}, {"total_song_num", diss == 901 ? 1 : 51}}}}}};
    };
    const auto first = f.Run("home");
    CollectionSchema(first);
    Expect(first["tracks"][0]["id"] == "qq:home0" && first["more"] == true && homeReads == 1,
        "QQ home publishes actual focused recommendation tracks and preserves their pagination");
    for (std::size_t i = 0; i < 5; ++i)
        Expect(first["recommendations"][i]["artwork"] ==
            "https://qpic.y.qq.com/music_cover/" + std::to_string(700 + i) + "/600",
            "QQ historical explicit cover fields and protocol-relative CDN URLs survive normalization");
    Expect(first["recommendations"][5]["artwork"] ==
        "https://y.gtimg.cn/music/photo_new/T002R300x300M000albumMID.jpg",
        "QQ cover MID uses the historical album CDN fallback");
    Expect(first["playlists"][0]["isLiked"] == true &&
        first["playlists"][0]["artwork"] == "https://qpic.y.qq.com/music_cover/newest/600" && coverReads == 1,
        "QQ 201 library cover is its first real song, even when the optional complete likes read fails");
    Expect(!first["playlists"][1].contains("isLiked") && !first["playlists"][2].contains("isLiked") &&
        !first.contains("likedIds"), "titles, other users' folders and failed snapshots cannot invent likes");
    const auto next = f.Run("home", {{"offset", 50}});
    Expect(next["tracks"][0]["id"] == "qq:home50" && next["more"] == false && homeReads == 2,
        "home append reads the same focused collection instead of switching to a later recommendation");
    const auto liked = f.Run("playlist", {{"id", "901"}});
    Expect(liked["tracks"][0]["id"] == "qq:favoriteMID" && !liked.contains("likedIds"),
        "QQ built-in playlist still returns real songs if the full likes snapshot is unavailable");
    Expect(f.Run("favorites")["tracks"] == liked["tracks"], "favorites and sidebar use the same historical route");
}
Json CatalogK(int index) {
    return {{"audio_info", {{"hash", HashHex("catalog-" + std::to_string(index))}, {"duration", 123000}}},
        {"base", {{"audio_name", "Fixture"}, {"album_id", 22}, {"album_audio_id", 33},
            {"mixsongid", 44}, {"album_name", "Album"}}},
        {"authors", Json::array({{{"base", {{"author_name", "Artist"}}}}})}};
}
void KugouCatalog() {
    Fixture f(Provider::Kugou);
    f.credentials["token"] = "synthetic-kugou-catalog";
    std::set<std::string> cards;
    int albumCalls = 0, historyCalls = 0, cardCalls = 0;
    f.answer = [&](const HttpRequest& r) -> Json {
        auto query = Query(r.url);
        const auto signature = query.at("signature");
        query.erase("signature");
        const std::string salt = "LnT6xpN3khm36zse0QzvmgTZ3waWdRSA";
        std::string signedText = salt;
        for (const auto& [key, value] : query) signedText += key + "=" + value;
        Expect(signature == HashHex(signedText + r.body + salt), "new Kugou routes retain exact signature");
        const auto p = Json::parse(r.body);
        if (r.url.find("/single_card_recommend") != r.url.npos) {
            ++cardCalls;
            cards.insert(query.at("card_id"));
            Expect(query.at("pagesize") == "30" && p == Json{{"tagid", ""}, {"u_info", ""}, {"source_mixsong", ""}},
                "Kugou cards use historical payload");
            Json rows = Json::array();
            for (int i = 0; i < 30; ++i) rows.push_back(CatalogK(i));
            return {{"status", 1}, {"data", {{"songs", rows}}}};
        }
        if (r.url.find("/v7/get_all_list") != r.url.npos)
            return {{"status", 1}, {"data", {{"total", 1}, {"info", Json::array({
                {{"musiclib_id", "22"}, {"source", 2}, {"name", "Album"}, {"type", 1}, {"listid", 7}}})}}}};
        if (r.url.find("/v1/album_audio/lite") != r.url.npos) {
            ++albumCalls;
            Expect(p["album_id"] == "22" && p["is_buy"] == "" && p["pagesize"] == 30 &&
                r.headers.at("x-router") == "openapi.kugou.com" && r.headers.at("kg-tid") == "255",
                "album route headers and no fabricated purchase");
            const int start = (p["page"].get<int>() - 1) * 30;
            Json rows = Json::array();
            for (int i = start; i < 123 && i < start + 30; ++i) rows.push_back(CatalogK(i));
            return {{"status", 1}, {"data", {{"songs", rows}, {"total", 123}}}};
        }
        Expect(r.url.find("/playhistory/v1/get_songs") != r.url.npos, "only exact history read route");
        ++historyCalls;
        Expect(p["userid"] == "77" && p["token"] == "synthetic-kugou-catalog" &&
            p["source_classify"] == "app" && p["to_subdivide_sr"] == 1, "history account and flags");
        const bool next = p.contains("bp");
        if (next) Expect(p["bp"] == "opaque-native-cursor", "history cursor replayed unchanged");
        Json rows = Json::array();
        for (int i = next ? 30 : 0; i < (next ? 65 : 30); ++i) rows.push_back(CatalogK(i));
        return {{"status", 1}, {"data", {{"list", rows}, {"bp", next ? "" : "opaque-native-cursor"}}}};
    };
    const auto home = f.Run("home");
    CollectionSchema(home);
    Expect(cards == std::set<std::string>{"3006", "3001", "3004", "3014", "3101", "3005"} &&
        home["recommendations"].size() == 6 && home["playlists"][0]["type"] == "album" &&
        home["playlists"][0]["owned"] == false, "six historical cards and subscribed album");
    const int cachedCalls = cardCalls;
    auto page = f.Run("playlist", {{"id", "kugou-card-3006"}, {"offset", 20}, {"limit", 20}});
    TrackSchema(page, f.provider);
    Expect(page["tracks"].size() == 10 && page["more"] == false && cardCalls == cachedCalls,
        "recommendation tracks slice cached snapshot");
    auto expected = HashHex("catalog-20");
    std::transform(expected.begin(), expected.end(), expected.begin(), [](char c) {
        return c >= 'a' && c <= 'f' ? static_cast<char>(c - 32) : c;
    });
    Expect(page["tracks"][0]["id"] == "kugou:" + expected, "card offset applied");
    page = f.Run("explore", {{"offset", 4}, {"limit", 2}});
    Expect(page["recommendations"].size() == 2 && page["recommendations"][0]["id"] == "kugou-card-3101" &&
        page["more"] == false, "recommendation collection offset");
    Error([&] { f.Run("playlist", {{"id", "kugou-card-9999"}}); }, "unsupported");
    page = f.Run("playlist", {{"id", "22"}, {"offset", 29}, {"limit", 100}});
    TrackSchema(page, f.provider);
    Expect(page["tracks"].size() == 94 && page["more"] == false && albumCalls == 5,
        "album arbitrary offset spans at most five 30-row pages");
    page = f.Run("playlist", {{"id", "22"}});
    Expect(page["tracks"].size() == 50 && page["more"] == true, "album default 50 assembled");
    page = f.Run("recent", {{"offset", 25}, {"limit", 30}});
    TrackSchema(page, f.provider);
    Expect(page["tracks"].size() == 30 && page["more"] == true && historyCalls == 2,
        "history crosses native cursor boundary");
    expected = HashHex("catalog-25");
    std::transform(expected.begin(), expected.end(), expected.begin(), [](char c) {
        return c >= 'a' && c <= 'f' ? static_cast<char>(c - 32) : c;
    });
    Expect(page["tracks"][0]["id"] == "kugou:" + expected, "history offset does not skip first batch");
    page = f.Run("recent", {{"offset", 60}, {"limit", 30}});
    Expect(page["tracks"].size() == 5 && page["more"] == false, "history final page");
    Error([&] { f.Run("recent", {{"offset", 1000}}); }, "unsupported");
    f.answer = [](const HttpRequest&) { return Json{{"status", 0}, {"error_code", 1}}; };
    Error([&] { f.Run("home"); }, "service");
    Error([&] { f.Run("playlist", {{"id", "22"}}); }, "service");
    Error([&] { f.Run("recent"); }, "service");
    f.answer = [](const HttpRequest&) { return Json{{"status", 1}, {"data", Json::object()}}; };
    Error([&] { f.Run("recent"); }, "invalid-response");
    f.answer = [](const HttpRequest&) {
        return Json{{"status", 1}, {"data", {{"list", Json::array({CatalogK(0)})}, {"bp", "stuck"}}}};
    };
    Error([&] { f.Run("recent"); }, "invalid-response");
    // Card snapshots must not cross the credential scope.
    f.credentials["token"] = "synthetic-other-account-token";
    f.answer = [](const HttpRequest&) { return Json{{"status", 0}}; };
    Error([&] { f.Run("playlist", {{"id", "kugou-card-3006"}}); }, "service");
}
void LikedSnapshots() {
    for (const auto provider : {Provider::Netease, Provider::QQ, Provider::Kugou}) {
        LikedFixture f(provider);
        Json expected = Json::array();
        for (int i = 0; i < 105; ++i) {
            f.rows.push_back(LikedTrack(provider, i == 99 ? 0 : i));
            if (i != 99) expected.push_back(LikedId(provider, i));
        }
        const auto first = f.Run("playlist", {{"id", provider == Provider::Netease ? "9" :
            provider == Provider::QQ ? "900" : "publicGid"}, {"limit", 1}});
        const int batches = provider == Provider::Netease ? 1 : 2;
        Expect(first["tracks"].size() == 1 && first["likedIds"] == expected && f.snapshotCalls == batches,
            "cold playlist returns a complete account snapshot, not its visible song IDs");
        Expect(f.snapshotOffsets == (provider == Provider::Netease ?
            std::vector<int>{0} : std::vector<int>{0, 100}), "snapshot pagination advances by raw, not deduplicated rows");
        Expect(f.Run("search", {{"query", "test"}, {"offset", 50}})["likedIds"] == expected &&
            f.Run("explore", {{"offset", 50}})["likedIds"] == expected &&
            f.Run("favorites", {{"offset", 50}})["likedIds"] == expected && f.snapshotCalls == batches,
            "appended pages and other lists reuse the same complete liked snapshot");
        if (provider != Provider::QQ)
            Expect(f.Run("recent")["likedIds"] == expected && f.snapshotCalls == batches,
                "recent history shares the account's complete liked snapshot");
        if (provider == Provider::QQ)
            f.Run("playlist", {{"id", "900"}});

        f.rows = Json::array({LikedTrack(provider, 1), LikedTrack(provider, 2)});
        expected = Json::array({LikedId(provider, 1), LikedId(provider, 2)});
        Expect(f.Run("favorites")["likedIds"] == expected && f.snapshotCalls == batches + 1,
            "favorites first page refreshes external changes exactly once on every provider");
        f.rows = Json::array({LikedTrack(provider, 3)});
        Expect(f.Run("home", {{"offset", 50}})["likedIds"] == expected && f.snapshotCalls == batches + 1,
            "home append retains the complete snapshot without querying it again");
        expected = Json::array({LikedId(provider, 3)});
        Expect(f.Run("home")["likedIds"] == expected && f.snapshotCalls == batches + 2,
            "explicit home refresh bypasses the cached snapshot and replaces external changes");

        const int beforeLike = f.snapshotCalls, beforeLibrary = f.libraryCalls;
        Expect(f.Run("like", {{"id", LikedId(provider, 0)}, {"value", true}})["likedIds"] ==
            Json::array({LikedId(provider, 3), LikedId(provider, 0)}),
            "successful like updates, rather than replaces, an existing complete snapshot");
        Expect(f.Run("like", {{"id", LikedId(provider, 0)}, {"value", false}})["likedIds"] == expected &&
            f.Run("search", {{"query", "test"}})["likedIds"] == expected &&
            f.snapshotCalls == beforeLike && f.libraryCalls == beforeLibrary,
            "unlike updates shared snapshot and reuses cached built-in collection metadata");

        const auto answer = f.answer;
        f.answer = [&](const HttpRequest& r) {
            if (f.IsWrite(r)) throw std::runtime_error("synthetic-write-failure");
            return answer(r);
        };
        Error([&] { f.Run("like", {{"id", LikedId(provider, 4)}, {"value", true}}); }, "service");
        Expect(f.Run("search", {{"query", "test"}})["likedIds"] == expected,
            "failed like cannot change the complete snapshot");
        f.answer = [&](const HttpRequest& r) {
            auto response = answer(r);
            if (f.IsSnapshot(r)) throw std::runtime_error("synthetic-snapshot-failure");
            return response;
        };
        Expect(f.Run("home")["likedIds"] == expected,
            "failed active refresh retains the previous complete snapshot, not an empty replacement");
        f.answer = answer;

        const auto credentials = f.credentials;
        f.credentials[Json::json_pointer(provider == Provider::Netease ? "/MUSIC_U" :
            provider == Provider::QQ ? "/credential/musicKey" : "/token")] = "synthetic-other-liked-account";
        f.rows = Json::array({LikedTrack(provider, 7)});
        const int beforeSwitch = f.snapshotCalls;
        Expect(f.Run("search", {{"query", "test"}})["likedIds"] == Json::array({LikedId(provider, 7)}) &&
            f.snapshotCalls == beforeSwitch + 1, "different credentials cannot reuse liked IDs");
        f.credentials = credentials;
        Expect(f.Run("search", {{"query", "test"}})["likedIds"] == expected && f.snapshotCalls == beforeSwitch + 1,
            "switching back restores only that credential scope's snapshot");
        if (provider == Provider::Netease) {
            Expect(f.Run("search", {{"query", "test"}, {"_userId", "88"}})["likedIds"] ==
                Json::array({LikedId(provider, 7)}) && f.snapshotCalls == beforeSwitch + 2,
                "NetEase snapshots also isolate the leased account identity");
            Expect(f.Run("search", {{"query", "test"}})["likedIds"] == expected,
                "another lease cannot overwrite the first NetEase account's snapshot");
        }
        f.rows = Json::array();
        Expect(f.Run("home")["likedIds"] == Json::array(), "a verified empty account snapshot is authoritative");
        const int beforeEmpty = f.snapshotCalls;
        Expect(f.Run("search", {{"query", "test"}})["likedIds"] == Json::array() &&
            f.snapshotCalls == beforeEmpty, "empty complete snapshots are cached too");
    }
}
void LikedSnapshotFailures() {
    for (const auto provider : {Provider::Netease, Provider::QQ, Provider::Kugou}) {
        LikedFixture cold(provider);
        cold.rows = Json::array({LikedTrack(provider, 1)});
        Expect(cold.Run("like", {{"id", LikedId(provider, 0)}, {"value", true}})["likedIds"] ==
            Json::array({LikedId(provider, 1), LikedId(provider, 0)}) && cold.snapshotCalls == 1,
            "standalone cross-provider like fetches a complete snapshot without any home/list prerequisite");
        const int libraryCalls = provider == Provider::Netease ? 0 : 1;
        Expect(cold.libraryCalls == libraryCalls &&
            cold.Run("like", {{"id", LikedId(provider, 0)}, {"value", false}})["likedIds"] ==
                Json::array({LikedId(provider, 1)}) && cold.libraryCalls == libraryCalls,
            "Kugou discovery stages likedCollection once and subsequent likes retain its canonical listId");

        LikedFixture unavailable(provider);
        unavailable.rows = Json::array({LikedTrack(provider, 1)});
        const auto available = unavailable.answer;
        unavailable.answer = [&](const HttpRequest& r) {
            auto response = available(r);
            if (unavailable.IsSnapshot(r)) throw std::runtime_error("synthetic-refresh-failure");
            return response;
        };
        const auto mutation = unavailable.Run("like", {{"id", LikedId(provider, 0)}, {"value", true}});
        Expect(mutation["liked"] == true && !mutation.contains("likedIds") && unavailable.snapshotCalls == 1,
            "cold like remains successful on refresh failure, without a partial snapshot or duplicate refresh");
        unavailable.answer = available;
        unavailable.rows.push_back(LikedTrack(provider, 0));
        Expect(unavailable.Run("search", {{"query", "test"}})["likedIds"] ==
            Json::array({LikedId(provider, 1), LikedId(provider, 0)}) && unavailable.snapshotCalls == 2,
            "later read retries after cold refresh failure instead of caching mutation-only IDs");

        LikedFixture partial(provider);
        for (int i = 0; i < 101; ++i) partial.rows.push_back(LikedTrack(provider, i));
        const auto complete = partial.answer;
        partial.answer = [&](const HttpRequest& r) {
            auto response = complete(r);
            if (partial.IsSnapshot(r) && (provider == Provider::Netease || partial.snapshotOffsets.back() == 100))
                throw std::runtime_error("synthetic-incomplete-snapshot");
            return response;
        };
        Expect(!partial.Run("search", {{"query", "test"}}).contains("likedIds"),
            "failed multi-page enumeration never publishes its first page as a complete snapshot");
        partial.answer = complete;
        partial.rows = Json::array({LikedTrack(provider, 5)});
        Expect(partial.Run("search", {{"query", "test"}})["likedIds"] == Json::array({LikedId(provider, 5)}),
            "a failed partial read cannot poison the next successful complete read");
        if (provider != Provider::Netease)
            Expect(partial.libraryCalls == 2, "failed snapshot discards discovery metadata staged by that attempt");

        LikedFixture cancelled(provider);
        for (int i = 0; i < 101; ++i) cancelled.rows.push_back(LikedTrack(provider, i));
        const auto active = cancelled.answer;
        cancelled.answer = [&](const HttpRequest& r) {
            auto response = active(r);
            if (cancelled.IsSnapshot(r) && (provider == Provider::Netease || cancelled.snapshotOffsets.back() == 100))
                cancelled.cancel = true;
            return response;
        };
        Error([&] { cancelled.Run("search", {{"query", "test"}}); }, "cancelled");
        cancelled.cancel = false;
        cancelled.answer = active;
        cancelled.rows = Json::array({LikedTrack(provider, 5)});
        Expect(cancelled.Run("search", {{"query", "test"}})["likedIds"] == Json::array({LikedId(provider, 5)}),
            "cancelled complete response cannot publish an account snapshot");
        if (provider != Provider::Netease)
            Expect(cancelled.libraryCalls == 2, "cancelled discovery never commits liked collection metadata");
        cancelled.rows = Json::array({LikedTrack(provider, 6)});
        cancelled.answer = [&](const HttpRequest& r) {
            auto response = active(r);
            if (cancelled.IsSnapshot(r)) cancelled.cancel = true;
            return response;
        };
        Error([&] { cancelled.Run("home"); }, "cancelled");
        cancelled.cancel = false;
        cancelled.answer = active;
        const int beforeRead = cancelled.snapshotCalls;
        Expect(cancelled.Run("search", {{"query", "test"}})["likedIds"] == Json::array({LikedId(provider, 5)}) &&
            cancelled.snapshotCalls == beforeRead, "cancelled refresh preserves the last committed complete snapshot");
    }
}
void LikesAction() {
    for (const auto provider : {Provider::Netease, Provider::QQ, Provider::Kugou}) {
        LikedFixture f(provider);
        Json expected = Json::array();
        for (int i = 0; i < 101; ++i) {
            f.rows.push_back(LikedTrack(provider, i));
            expected.push_back(LikedId(provider, i));
        }
        const auto answer = f.answer;
        f.answer = [&](const HttpRequest& r) {
            const bool discovery = provider == Provider::QQ ?
                r.url.find("fcg_user_created_diss") != r.url.npos ||
                    r.url.find("fcg_get_profile_order_asset") != r.url.npos :
                provider == Provider::Kugou && r.url.find("/v7/get_all_list") != r.url.npos;
            Expect(f.IsSnapshot(r) || discovery, "likes never fetches a browsing page or song details");
            return answer(r);
        };
        auto result = f.Run("likes");
        const int batches = provider == Provider::Netease ? 1 : 2;
        Expect(result == Json{{"tracks", Json::array()}, {"playlists", Json::array()},
            {"recommendations", Json::array()}, {"more", false}, {"likedIds", expected}} &&
            f.snapshotCalls == batches, "likes returns only an empty standard page and complete account IDs");
        f.rows = Json::array({LikedTrack(provider, 9)});
        expected = Json::array({LikedId(provider, 9)});
        Expect(f.Run("likes")["likedIds"] == expected && f.snapshotCalls == batches + 1,
            "likes explicitly refreshes even when a complete snapshot is already cached");
        const auto snapshotOnly = f.answer;
        f.answer = [&](const HttpRequest& r) {
            auto response = snapshotOnly(r);
            if (f.IsSnapshot(r)) throw std::runtime_error("synthetic-likes-refresh-failure");
            return response;
        };
        Expect(f.Run("likes")["likedIds"] == expected,
            "likes refresh failure may retain an existing complete account snapshot");
        f.rows = Json::array({LikedTrack(provider, 10)});
        f.answer = [&](const HttpRequest& r) {
            auto response = snapshotOnly(r);
            if (f.IsSnapshot(r)) f.cancel = true;
            return response;
        };
        Error([&] { f.Run("likes"); }, "cancelled");
        f.cancel = false;
        f.answer = answer;
        Expect(f.Run("search", {{"query", "test"}})["likedIds"] == expected,
            "cancelled likes cannot overwrite the previously committed complete snapshot");
        f.rows = Json::array();
        f.answer = snapshotOnly;
        Expect(f.Run("likes")["likedIds"] == Json::array(), "likes distinguishes a verified empty snapshot from failure");

        LikedFixture unavailable(provider);
        unavailable.answer = [](const HttpRequest&) -> Json { throw std::runtime_error("synthetic-unavailable"); };
        Error([&] { unavailable.Run("likes"); }, "unavailable");
    }
}
void Boundaries() {
    int calls = 0;
    Exchange noNetwork = [&](const HttpRequest&, const Cancelled&) -> HttpResponse {
        ++calls; throw std::runtime_error("must not reach transport");
    };
    for (const auto provider : {Provider::Netease, Provider::QQ, Provider::Kugou}) {
        const auto credentials = Credentials(provider);
        for (const auto& args : {
                Json{{"id", "https://evil.test/song"}}, Json{{"id", "../42"}},
                Json{{"id", "other:42"}}, Json{{"id", "42"}, {"url", "https://evil.test/"}}})
            Error([&] { Execute(provider, "resolve", args, credentials, {}, noNetwork); }, "invalid-response");
        Error([&] { Execute(provider, "like", {{"id", "42"}, {"value", "true"}}, credentials, {}, noNetwork); }, "invalid-response");
        Error([&] { Execute(provider, "search", {{"query", "test"}, {"offset", -1}}, credentials, {}, noNetwork); }, "invalid-response");
        Error([&] { Execute(provider, "search", {{"query", "test"}, {"limit", 0}}, credentials, {}, noNetwork); }, "invalid-response");
        Error([&] { Execute(provider, "search", {{"query", "test"}, {"limit", 1.5}}, credentials, {}, noNetwork); }, "invalid-response");
        Error([&] { Execute(provider, "search", {{"query", "test"}, {"offset", 100001}}, credentials, {}, noNetwork); }, "invalid-response");
        for (const auto& quality : {Json("auto"), Json("hires\n"), Json(320), Json(nullptr)})
            Error([&] { Execute(provider, "resolve", {{"id", "42"}, {"quality", quality}},
                credentials, {}, noNetwork); }, "invalid-response");
        if (provider != Provider::Netease) for (const auto* quality : {"jyeffect", "sky", "jymaster"})
            Error([&] { Execute(provider, "resolve", {{"id", "42"}, {"quality", quality}},
                credentials, {}, noNetwork); }, "invalid-response");
        Error([&] { Execute(provider, "home", {{"quality", "hires"}}, credentials, {}, noNetwork); }, "invalid-response");
        for (const auto& args : {Json{{"offset", 0}}, Json{{"limit", 50}},
                Json{{"id", "42"}}, Json{{"query", "test"}}})
            Error([&] { Execute(provider, "likes", args, credentials, {}, noNetwork); }, "invalid-response");
        Error([&] { Execute(provider, "unknown", Json::object(), credentials, {}, noNetwork); }, "unsupported");
        Error([&] { Execute(provider, "home", Json::object(), credentials, [] { return true; }, noNetwork); }, "cancelled");
        Error([&] { Execute(provider, "like", {{"id", "42"}, {"value", true}}, Json::object(), {}, noNetwork); }, "expired");
    }
    Expect(calls == 0, "rejected input performs zero requests");
    auto n = Credentials(Provider::Netease);
    for (const auto& response : {HttpResponse{200, std::string(1024 * 1024 + 1, 'x'), "", {}, {}},
        HttpResponse{200, "<html>no</html>", "", {}, {}}, HttpResponse{200, "[]", "", {}, {}}})
        Error([&] { Execute(Provider::Netease, "explore", Json::object(), n, {},
            [&](const HttpRequest&, const Cancelled&) { return response; }); }, "invalid-response");
    for (unsigned status : {301U, 401U, 500U})
        Error([&] { Execute(Provider::Netease, "explore", Json::object(), n, {},
            [&](const HttpRequest&, const Cancelled&) { return HttpResponse{status, "{}", "", {}, {}}; }); },
            status == 401 ? "expired" : "service");
    bool cancel = false;
    Error([&] { Execute(Provider::Netease, "explore", Json::object(), n, [&] { return cancel; },
        [&](const HttpRequest&, const Cancelled&) { cancel = true; return Response({{"code", 200}, {"playlists", Json::array()}}); }); },
        "cancelled");
    Json rows = Json::array();
    for (int i = 0; i < 150; ++i) rows.push_back({{"id", i + 1}, {"name", "Fixture"}});
    const auto bounded = Execute(Provider::Netease, "explore", {{"limit", 900}}, n, {},
        [&](const HttpRequest&, const Cancelled&) { return Response({{"code", 200}, {"playlists", rows}, {"more", true}}); });
    Expect(bounded["playlists"].size() == 100, "upstream oversize list capped at 100");
    for (const auto* url : {"https://evil.test/a", "https://music.126.net.evil.test/a",
            "https://user@music.126.net/a", "file:///C:/secret", "http://127.0.0.1/a"}) {
        Error([&] { Execute(Provider::Netease, "resolve", {{"id", "42"}}, n, {},
            [&](const HttpRequest&, const Cancelled&) { return Response({{"code", 200},
                {"data", Json::array({{{"id", 42}, {"url", url}, {"code", 200}}})}}); }); }, "unavailable");
    }
    Error([&] { Execute(Provider::Netease, "resolve", {{"id", "42"}}, n, {},
        [&](const HttpRequest&, const Cancelled&) { return Response({{"code", 200},
            {"data", Json::array({{{"id", 42}, {"url", "https://m7.music.126.net/preview.mp3"},
                {"freeTrialInfo", {{"start", 0}, {"end", 30}}}}})}}); }); }, "unavailable");
    auto credentials = Credentials(Provider::QQ);
    Error([&] { Execute(Provider::QQ, "home", {{"_userId", "78"}}, credentials, {}, noNetwork); }, "expired");
    Error([&] { Execute(Provider::QQ, "addToPlaylist", {{"id", "900"}, {"trackId", "qq:songMID"}},
        Json{{"credential", {{"uin", "88"}, {"musicKey", "other-account"}}}}, {},
        [&](const HttpRequest&, const Cancelled&) {
            return Response({{"code", 0}, {"req_0", {{"code", 0}, {"data", {{"track_info", TrackQ()}}}}}});
        }); }, "unavailable");
    const auto anonymous = Execute(Provider::Kugou, "search", {{"query", "test"}}, Json::object(), {},
        [&](const HttpRequest& r, const Cancelled&) {
            Expect(ValidHttpRequest(r) && r.url.find("https://complexsearch.kugou.com/v2/search/song?") == 0,
                "anonymous Kugou is fixed HTTPS historical route");
            const auto params = Query(r.url);
            Expect(params.at("userid") == "0" && params.at("token").empty() &&
                params.at("clientver") == "11070", "anonymous request never gets stored credentials");
            return Response({{"status", 1}, {"data", {{"info", Json::array({TrackK()})}, {"total", 1}}}});
        });
    TrackSchema(anonymous, Provider::Kugou);
    for (const auto p : {Provider::QQ, Provider::Kugou}) {
        const auto creds = Json::object();
        Json songs = Json::array();
        for (int i = 0; i < 130; ++i) {
            auto row = p == Provider::QQ ? TrackQ() : TrackK();
            if (p == Provider::QQ) row["mid"] = "mid" + std::to_string(i);
            else row["FileHash"] = HashHex(std::to_string(i));
            songs.push_back(row);
        }
        auto page = Execute(p, "search", {{"query", "test"}, {"limit", 1000}}, creds, {},
            [&](const HttpRequest& r, const Cancelled&) {
                if (p == Provider::QQ) {
                    Expect(Json::parse(r.body)["req_0"]["param"]["num_per_page"] == 100, "QQ wire limit capped");
                    return Response({{"code", 0}, {"req_0", {{"code", 0},
                        {"data", {{"body", {{"item_song", songs}}}, {"meta", {{"sum", 200}}}}}}}});
                }
                Expect(Query(r.url)["pagesize"] == "100", "Kugou wire limit capped");
                return Response({{"status", 1}, {"data", {{"info", songs}, {"total", 200}}}});
            });
        Expect(page["tracks"].size() == 100 && page["more"] == true, "track page hard bound");
        Error([&] { Execute(p, "search", {{"query", "test"}}, creds, {},
            [&](const HttpRequest&, const Cancelled&) { return HttpResponse{200, std::string(1024 * 1024 + 1, 'x'), "", {}, {}}; }); },
            "invalid-response");
    }
    for (int code : {104009, 104003}) {
        Error([&] { Execute(Provider::QQ, "resolve", {{"id", "songMID"}}, Credentials(Provider::QQ), {},
            [&](const HttpRequest& r, const Cancelled&) {
                const auto method = Json::parse(r.body)["req_0"]["method"];
                if (method == "get_song_detail_yqq")
                    return Response({{"code", 0}, {"req_0", {{"code", 0}, {"data", {{"track_info", TrackQ()}}}}}});
                return Response({{"code", 0}, {"req_0", {{"code", 0}, {"data", {{"retcode", code},
                    {"midurlinfo", Json::array({{{"result", code}, {"purl", "blocked.m4a"}}})},
                    {"sip", Json::array({"https://isure.stream.qqmusic.qq.com/"})}}}}}});
            }); }, code == 104009 ? "expired" : "unavailable");
    }
    Error([&] { Execute(Provider::Kugou, "resolve", {{"id", hash}}, Credentials(Provider::Kugou), {},
        [&](const HttpRequest&, const Cancelled&) {
            return Response({{"status", 1}, {"is_free_part", 1}, {"url", Json::array({"https://fs.w.kugou.com/preview.mp3"})}});
        }); }, "unavailable");
    Error([&] { Execute(Provider::Kugou, "resolve", {{"id", hash}}, Credentials(Provider::Kugou), {},
        [&](const HttpRequest&, const Cancelled&) {
            return Response({{"status", 1}, {"url", Json::array({"https://kugou.com.evil.test/song.mp3"})}});
        }); }, "unavailable");
}
void Routes() {
    struct Route { Provider provider; const char* method; const char* url; const char* router; };
    const Route routes[] = {
        {Provider::Netease, "POST", "https://music.163.com/weapi/playlist/create", ""},
        {Provider::Netease, "POST", "https://interfacepc.music.163.com/eapi/cloudsearch/pc", ""},
        {Provider::Netease, "POST", "https://interfacepc.music.163.com/eapi/song/enhance/player/url/v1", ""},
        {Provider::QQ, "GET", "https://c.y.qq.com/splcloud/fcgi-bin/fcg_get_diss_by_tag.fcg", ""},
        {Provider::QQ, "GET", "https://c.y.qq.com/fav/fcgi-bin/fcg_get_profile_order_asset.fcg", ""},
        {Provider::QQ, "POST", "https://u.y.qq.com/cgi-bin/musicu.fcg", ""},
        {Provider::Kugou, "GET", "https://gateway.kugou.com/v3/search/song", "complexsearch.kugou.com"},
        {Provider::Kugou, "GET", "https://gateway.kugou.com/v5/url", "trackercdn.kugou.com"},
        {Provider::Kugou, "POST", "https://gateway.kugou.com/v4/delete_songs", "cloudlist.service.kugou.com"},
        {Provider::Kugou, "POST", "https://gateway.kugou.com/v1/album_audio/lite", "openapi.kugou.com"},
        {Provider::Kugou, "POST", "https://gateway.kugou.com/playhistory/v1/get_songs", ""},
        {Provider::Kugou, "GET", "https://lyrics.kugou.com/download", ""}
    };
    for (const auto& route : routes) {
        HttpRequest r{route.provider, route.method, route.url, {}, {}, 1024};
        if (*route.router) r.headers["x-router"] = route.router;
        Expect(ValidHttpRequest(r), "new exact route accepted");
        auto bad = r; bad.method = r.method == "POST" ? "GET" : "POST";
        Expect(!ValidHttpRequest(bad), "wrong route method rejected");
        bad = r; bad.provider = r.provider == Provider::QQ ? Provider::Kugou : Provider::QQ;
        Expect(!ValidHttpRequest(bad), "cross provider route rejected");
        bad = r; bad.headers["x-router"] = "evil.test";
        Expect(!ValidHttpRequest(bad), "router replacement rejected");
        bad = r; bad.url.replace(0, 8, "http://");
        Expect(!ValidHttpRequest(bad), "plaintext route rejected");
        bad = r; const auto slash = bad.url.find('/', 8); bad.url.insert(slash, ".evil.test");
        Expect(!ValidHttpRequest(bad), "lookalike host rejected");
        bad = r; bad.headers["kg-tid"] = "255";
        Expect(ValidHttpRequest(bad) == (r.url == "https://gateway.kugou.com/v1/album_audio/lite"),
            "album header allowed on its one route only");
        bad.headers["kg-tid"] = "1";
        Expect(!ValidHttpRequest(bad), "arbitrary album header rejected");
    }
}
void QualityAndLikedSnapshots() {
    Fixture n(Provider::Netease);
    std::string expected;
    n.answer = [&](const HttpRequest& request) {
        const auto args = Eapi(request);
        Expect(args["level"] == expected && !args.contains("br"), "NetEase sends the requested real quality level");
        return Json{{"code", 200}, {"data", Json::array({{{"id", 42},
            {"url", "https://m7.music.126.net/audio.flac"}, {"level", "exhigh"}, {"br", 320000}}})}};
    };
    for (const auto* quality : {"standard", "high", "lossless", "hires", "jyeffect", "sky", "jymaster"}) {
        expected = std::string_view(quality) == "high" ? "exhigh" : quality;
        const auto source = n.Run("resolve", {{"id", "42"}, {"quality", quality}});
        Expect(source["quality"] == "high" && source["bitrate"] == 320000,
            "NetEase reports a server downgrade, never the requested quality");
    }
    n.answer = [&](const HttpRequest& request) {
        const auto args = Eapi(request);
        Expect(args["level"] == expected, "NetEase exclusive quality is passed through unchanged");
        return Json{{"code", 200}, {"data", Json::array({{{"id", 42},
            {"url", "https://m7.music.126.net/audio.flac"}, {"level", expected}, {"br", 2400000}}})}};
    };
    for (const auto* quality : {"jyeffect", "sky", "jymaster"}) {
        expected = quality;
        Expect(n.Run("resolve", {{"id", "42"}, {"quality", quality}})["quality"] == quality,
            "NetEase exclusive server quality is reported as actual");
    }
    Fixture q(Provider::QQ);
    std::string returned = "RS01mediaMID.flac?vkey=fixture";
    q.answer = [&](const HttpRequest& request) {
        const auto wire = Json::parse(request.body);
        const auto& args = wire["req_0"]["param"];
        Json data;
        if (wire["req_0"]["method"] == "get_song_detail_yqq") data = {{"track_info", TrackQ()}};
        else {
            const auto names = args["filename"];
            const auto quality = expected;
            const auto count = quality == "hires" ? 5 : quality == "lossless" ? 4 : quality == "high" ? 3 : 2;
            Expect(names.size() == static_cast<std::size_t>(count) && args["songmid"].size() == names.size() &&
                args["songtype"].size() == names.size(), "QQ aligned real vkey variants");
            const auto first = quality == "hires" ? "RS01mediaMID.flac" : quality == "lossless" ?
                "F000mediaMID.flac" : quality == "high" ? "M800mediaMID.mp3" : "C400mediaMID.m4a";
            Expect(names[0] == first, "QQ requests the actual preferred filename");
            data = {{"retcode", 0}, {"sip", Json::array({"https://isure.stream.qqmusic.qq.com/"})},
                {"midurlinfo", Json::array({{{"purl", returned}, {"songmid", "songMID"}}})}};
        }
        return Json{{"code", 0}, {"req_0", {{"code", 0}, {"data", data}}}};
    };
    for (const auto* quality : {"standard", "high", "lossless", "hires"}) {
        expected = quality;
        returned = "M500mediaMID.mp3?vkey=fixture";
        Expect(q.Run("resolve", {{"id", "songMID"}, {"quality", quality}})["quality"] == "standard",
            "QQ actual filename identifies fallback quality");
    }
    expected = "hires";
    returned = "RS01mediaMID.flac?vkey=fixture";
    Expect(q.Run("resolve", {{"id", "songMID"}, {"quality", "hires"}})["quality"] == "hires", "QQ real Hi-Res URL");
    expected = "lossless";
    returned = "A000mediaMID.flac?vkey=fixture";
    Expect(q.Run("resolve", {{"id", "songMID"}, {"quality", "lossless"}})["quality"] == "lossless",
        "QQ A000 URL identifies lossless quality");
    expected = "hires";
    returned = "unknown.mp3";
    Expect(q.Run("resolve", {{"id", "songMID"}, {"quality", "hires"}})["quality"] == "", "unknown QQ URL stays unknown");
    Fixture k(Provider::Kugou);
    std::vector<std::string> attempts;
    k.answer = [&](const HttpRequest& request) {
        const auto params = Query(request.url);
        const auto quality = params.at("quality");
        attempts.push_back(quality + ":" + params.at("album_id"));
        if (quality != "320") return Json{{"status", 1}, {"url", Json::array()}};
        return Json{{"status", 1}, {"data", {{"url", "https://fs.w.kugou.com/audio.mp3"},
            {"bitrate", 320000}, {"type", "mp3"}}}};
    };
    const auto source = k.Run("resolve", {{"id", hash}, {"quality", "hires"}});
    Expect(source["quality"] == "high" && source["bitrate"] == 320000 &&
        attempts.front().rfind("high:", 0) == 0 && attempts.back().rfind("320:", 0) == 0,
        "Kugou really retries Hi-Res, lossless and high, reporting returned bitrate");
    attempts.clear();
    k.answer = [&](const HttpRequest& request) {
        const auto quality = Query(request.url).at("quality");
        attempts.push_back(quality);
        return Json{{"status", 1}, {"url", "https://fs.w.kugou.com/audio"}};
    };
    for (const auto* quality : {"standard", "high", "lossless", "hires"}) {
        attempts.clear();
        const auto sourceUnknown = k.Run("resolve", {{"id", hash}, {"quality", quality}});
        Expect(sourceUnknown["quality"] == "" && sourceUnknown["bitrate"] == 0, "Kugou cannot invent actual quality");
        const auto wire = std::string_view(quality) == "standard" ? "128" : std::string_view(quality) == "high" ?
            "320" : std::string_view(quality) == "lossless" ? "flac" : "high";
        Expect(attempts[0] == wire, "Kugou requested wire quality");
    }
    Fixture kCompat(Provider::Kugou);
    kCompat.answer = [](const HttpRequest& request) {
        if (request.url.find("/v5/url") != request.url.npos)
            return Json{{"status", 1}, {"data", {{"playUrl",
                "http://fs.w.kugou.com/primary.mp3,http://fs.w.kugou.com/backup.mp3"}}}};
        return Json{{"status", 1}, {"data", {{"info", Json::array({TrackK()})}, {"total", 1}}}};
    };
    Expect(kCompat.Run("resolve", {{"id", hash}})["url"] == "http://fs.w.kugou.com/primary.mp3",
        "Kugou accepts playUrl and selects the first comma-separated URL");
    n.answer = [](const HttpRequest& r) {
        if (r.url.find("/song/like/get") != r.url.npos)
            return Json{{"code", 200}, {"ids", Json::array({42, 42, 43})}};
        if (r.url.find("/user/playlist") != r.url.npos)
            return Json{{"code", 200}, {"playlist", Json::array()}, {"more", false}};
        return Json{{"code", 200}, {"result", Json::array()}};
    };
    Expect(n.Run("home")["likedIds"] == Json::array({"netease:42", "netease:43"}),
        "home reads complete stable liked IDs without song details");
    Expect(n.Run("home", {{"offset", 50}})["likedIds"] == Json::array({"netease:42", "netease:43"}),
        "append home reuses the complete liked snapshot");
    n.answer = [](const HttpRequest& r) {
        if (r.url.find("/song/like/get") != r.url.npos) return Json{{"code", 500}};
        if (r.url.find("/user/playlist") != r.url.npos)
            return Json{{"code", 200}, {"playlist", Json::array()}};
        return Json{{"code", 200}, {"result", Json::array({{{"id", 9}, {"name", "Recommended"}}})}};
    };
    const auto home = n.Run("home");
    Expect(home["likedIds"] == Json::array({"netease:42", "netease:43"}) &&
        home["recommendations"].size() == 1,
        "liked refresh failure preserves the last complete snapshot");
}
void KugouPlayback() {
    Fixture f(Provider::Kugou);
    const std::string url = "http://fs.youthandroid2.kugou.com/song.mp3?key=fixture";
    for (const auto* field : {"url", "play_url", "playUrl"}) {
        for (const int envelope : {0, 1, 2}) {
            for (const bool array : {false, true}) {
                f.answer = [&](const HttpRequest& r) {
                    const auto params = Query(r.url);
                    Expect(ValidHttpRequest(r) && r.headers.at("x-router") == "trackercdn.kugou.com" &&
                        params.at("hash") == std::string(32, 'a') && params.at("IsFreePart") == "0" &&
                        params.at("token") == "synthetic-kugou-secret" && params.at("userid") == "77",
                        "Kugou playback retains authenticated historical routing");
                    auto signedParams = params;
                    const auto signature = signedParams.at("signature");
                    signedParams.erase("signature");
                    const std::string salt = "LnT6xpN3khm36zse0QzvmgTZ3waWdRSA";
                    auto plain = salt;
                    for (const auto& [key, value] : signedParams) plain += key + "=" + value;
                    Expect(signature == HashHex(plain + salt) &&
                        params.at("key") == HashHex(std::string(32, 'a') +
                            "185672dd44712f60bb1736df5a377e82" + "3116" + "123456" + "77"),
                        "Kugou signature and authorization key match the historical bridge");
                    Json payload = {{field, array ? Json::array({"invalid", url}) : Json(url)},
                        {"bitrate", 128000}};
                    if (envelope == 0) { payload["status"] = 1; return payload; }
                    return Json{{"status", 1}, {"data", envelope == 1 ? payload : Json::array({payload})}};
                };
                const auto source = f.Run("resolve", {{"id", hash}});
                Expect(source["url"] == url && source["quality"] == "standard" && source["bitrate"] == 128000,
                    "Kugou URL scalar/array and response envelopes preserve the HTTP media source");
            }
        }
    }
    f.answer = [&](const HttpRequest& r) {
        if (r.url.find("/v5/url") == r.url.npos)
            return Json{{"status", 1}, {"data", {{"info", Json::array({TrackK()})}, {"total", 1}}}};
        const auto params = Query(r.url);
        if (params.at("album_id") == "22") {
            Expect(params.at("album_audio_id") == "33", "Kugou uses the catalog metadata first");
            return Json{{"status", 0}, {"error_code", 30020}};
        }
        Expect(params.at("album_id") == "0" && params.at("album_audio_id") == "0",
            "Kugou hash-only retry signs numeric zero like the historical song_url module");
        return Json{{"status", 1}, {"url", url}};
    };
    f.Run("search", {{"query", "fixture"}});
    Expect(f.Run("resolve", {{"id", hash}})["url"] == url,
        "Kugou retries mismatched catalog metadata with the authorized hash alone");
    for (const auto* unsafe : {"http://fs.kugou.com.evil.test/song", "http://fs.evil.test/song",
            "http://user@fs.w.kugou.com/song", "http://127.0.0.1/song", "http://fs.w.kugou.com:80/song",
            "http://fs.w.kugou.com/song\r\nInjected: x"}) {
        f.answer = [&](const HttpRequest&) { return Json{{"status", 1}, {"url", unsafe}}; };
        Error([&] { f.Run("resolve", {{"id", hash}}); }, "unavailable");
    }
    for (const auto* preview : {"is_free_part", "isFreePart"}) {
        f.answer = [&](const HttpRequest&) {
            return Json{{"status", 1}, {"data", {{preview, 1}, {"url", url}}}};
        };
        Error([&] { f.Run("resolve", {{"id", hash}}); }, "unavailable");
    }
}
}  // namespace
int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--kugou-playback") {
        KugouPlayback();
        std::cout << "Kugou playback protocol, signature, metadata retry and security regressions passed\n";
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--live-qq-lyrics-read-only") {
        try {
            // Public song fixtures from yt-dlp 2025.09.26; no profile, cookie store or media request.
            for (const auto* id : {"004Ti8rT003TaZ", "004MsGEo3DdNxV"}) {
                const auto raw = Execute(Provider::QQ, "lyrics", {{"id", id}}, Json::object(), {});
                const auto parsed = ParseOnlineLyrics(Provider::QQ, raw);
                Expect(raw["request"]["data"]["crypt"] == 0 && parsed.error.empty() && !parsed.lines.empty(),
                    "live QQ response is unencrypted timed LRC");
                for (const auto& line : parsed.lines) Expect(line.words.empty(), "no invented word times");
                std::cout << "qq anonymous LRC: crypt=0, lines=" << parsed.lines.size()
                    << ", firstStart=" << parsed.lines.front().startTime
                    << ", lastStart=" << parsed.lines.back().startTime << ", words=0\n";
            }
            return 0;
        } catch (const std::exception& e) {
            std::cout << "qq anonymous LRC: " << e.what() << '\n';
            return 1;
        }
    }
    if (argc == 2 && std::string_view(argv[1]) == "--live-lyrics-read-only") {
        std::string stage = "search";
        try {
            const auto page = Execute(Provider::Kugou, "search",
                {{"query", u8"\u6674\u5929"}, {"limit", 1}}, Json::object(), {});
            if (page.at("tracks").empty()) throw std::runtime_error("unavailable");
            const auto& track = page["tracks"][0];
            stage = "lyric-search";
            const auto raw = Execute(Provider::Kugou, "lyrics", {{"id", track["id"]}}, Json::object(), {},
                [&](const HttpRequest& request, const Cancelled& cancelled) {
                    const bool download = request.url.find("https://lyrics.kugou.com/download?") == 0;
                    stage = download ? "download" : "lyric-search";
                    const auto response = lume::music::auth::SendHttp(request, cancelled);
                    const auto diagnostic = Json::parse(response.body, nullptr, false);
                    std::cout << stage << " http=" << response.status << ", bytes=" << response.body.size()
                        << ", json=" << diagnostic.type_name() << '\n';
                    if (!download && diagnostic.is_object()) {
                        for (auto it = diagnostic.begin(); it != diagnostic.end(); ++it)
                            std::cout << "  field=" << it.key() << ", type=" << it.value().type_name() << '\n';
                    }
                    if (download) {
                        const auto data = Json::parse(response.body, nullptr, false);
                        if (data.is_object()) {
                            const auto parsed = ParseOnlineLyrics(Provider::Kugou, data, track["duration"].get<double>());
                            std::cout << "download http=" << response.status << ", parserError=" << parsed.error
                                << ", parsedLines=" << parsed.lines.size();
                            for (const auto* key : {"status", "contenttype", "error_code"})
                                if (data.contains(key) && data[key].is_number()) std::cout << ", " << key << '=' << data[key];
                            std::cout << '\n';
                        }
                    }
                    return response;
                });
            const auto parsed = ParseOnlineLyrics(Provider::Kugou, raw, track["duration"].get<double>());
            Expect(parsed.error.empty() && !parsed.lines.empty(), "live LRC parses");
            for (const auto& line : parsed.lines) Expect(line.words.empty(), "live fallback is line-timed only");
            std::cout << "kugou anonymous fmt=lrc: lines=" << parsed.lines.size()
                << ", firstStart=" << parsed.lines.front().startTime
                << ", lastStart=" << parsed.lines.back().startTime << ", words=0\n";
            return 0;
        } catch (const std::exception& e) {
            std::cout << "kugou anonymous fmt=lrc: stage=" << stage << ", error=" << e.what() << '\n';
            return 1;
        }
    }
    if (argc == 2 && std::string_view(argv[1]) == "--live-read-only") {
        int failed = 0;
        for (auto provider : {Provider::Netease, Provider::QQ, Provider::Kugou}) {
            try {
                const auto page = Execute(provider, "search", {{"query", "test"}, {"limit", 2}},
                    Json::object(), {}, lume::music::auth::SendHttp);
                std::cout << Name(provider) << ": tracks=" << page.at("tracks").size()
                    << ", more=" << page.at("more") << '\n';
            } catch (const std::exception& e) {
                ++failed;
                std::cout << Name(provider) << ": " << e.what() << '\n';
            }
        }
        return failed ? 1 : 0;
    }
    QQLegacyCharset();
    Netease();
    QQ();
    Kugou();
    KugouPlayback();
    QQCollections();
    QQHomeAndLikedCollection();
    KugouCatalog();
    LyricsFallback();
    QQPublicLyrics();
    Boundaries();
    CatalogResilience();
    QQEmptyLibraryIsAnError();
    QQPartialLibraryKeepsCollectedLists();
    KugouShortPagesContinue();
    QualityAndLikedSnapshots();
    LikedSnapshots();
    LikedSnapshotFailures();
    LikesAction();
    Routes();
    std::cout << "online provider protocol, schema, mutation, isolation and security tests passed (fake transport)\n";
}
