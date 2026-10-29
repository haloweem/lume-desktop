// Local diagnostic probe for the music providers. Not shipped and not a test.
//
// Reads the signed-in accounts under %LOCALAPPDATA%\Lume\music-accounts (DPAPI,
// same Windows user), then issues read-only requests through the production
// provider code: profile/membership, a search, and audio resolution at every
// quality level. Every request and response is written redacted to a JSON
// lines file. Credentials are never saved back, and any login, token refresh
// or write endpoint is refused before it reaches the network, so the stored
// session cannot be rotated or changed by running this probe.
#include "platform/windows/music_provider_credentials.h"
#include "platform/windows/music_provider_http.h"
#include "platform/windows/music_online_provider.h"
#include "core/music/music_types.h"
#include "platform/windows/music_audio_engine.h"
#include <chrono>
#include <thread>
#include <windows.h>
#include <wininet.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <functional>
#include <regex>
#include <string>
#include <vector>

namespace {
using namespace lume::music;
using auth::Json;

// Identifier values taken from the stored account (QQ number, device IDs...),
// removed from every line whatever field or map key carries them.
std::vector<std::string> sensitiveValues;

std::string Redact(std::string text) {
    static const std::regex keyValue(
        R"re(("?(?:musickey|qqmusic_key|qm_keyst|authst|access_token|accesstoken|refresh_token|refreshtoken|refreshkey|refresh_key|openid|unionid|token|music_u|music_a|__csrf|csrf_token|cookie|vkey|sign|signature|key|password|guid|uuid|mid|dfid|qimei|qimei36|openudid|openudid2|udid|aid|qq|uuin|wxopenid|sid|rom|phonetype|imei|android_id|mac|deviceid|device_id|userid|uin|musicid|str_musicid|nickname|nick|logo|avatar|headpic)"?\s*[:=]\s*"?)([^"&;,\s}]+))re",
        std::regex::icase);
    text = std::regex_replace(text, keyValue, "$1***");
    for (const auto& secret : sensitiveValues)
        for (std::size_t at; !secret.empty() && (at = text.find(secret)) != std::string::npos;)
            text.replace(at, secret.size(), "***");
    // Signed media URLs: keep scheme, host and file name, drop the query.
    static const std::regex signedQuery(R"((https?://[^\s"?]+)\?[^\s"]*)");
    text = std::regex_replace(text, signedQuery, "$1?***");
    return text;
}

bool Forbidden(const auth::HttpRequest& request) {
    const auto& u = request.url;
    const auto& b = request.body;
    // QQ login/refresh, NetEase token refresh/login, KuGou token refresh, and
    // every write action the provider supports.
    for (const char* marker : {"music.login.LoginServer", "LoginServer", "/login", "token/refresh",
            "/weapi/login", "/eapi/login", "refresh_token", "like", "playlist/manipulate", "createPlaylist",
            "addToPlaylist", "music.musicasset.SongFavWrite", "PlaylistDetailWrite", "/v1/radio/like"})
        if (u.find(marker) != std::string::npos || b.find(marker) != std::string::npos) return true;
    return false;
}

std::ofstream logFile;
auth::HttpResponse Exchange(const auth::HttpRequest& request, const auth::Cancelled& cancelled) {
    Json entry{{"provider", auth::Name(request.provider)}, {"method", request.method},
        {"url", Redact(request.url)}, {"body", Redact(request.body.substr(0, 4096))}};
    if (Forbidden(request)) {
        entry["refused"] = true;
        logFile << entry.dump() << '\n' << std::flush;
        throw std::runtime_error("unavailable");
    }
    auto response = auth::SendHttp(request, cancelled);
    entry["status"] = response.status;
    entry["response"] = Redact(response.body.substr(0, 16384));
    logFile << entry.dump(-1, ' ', false, Json::error_handler_t::replace) << '\n' << std::flush;
    return response;
}

// Step-by-step Media Foundation negotiation, reported without the URL.
Json DescribeSource(const std::string& url) {
    using Microsoft::WRL::ComPtr;
    Json out;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION, MFSTARTUP_FULL);
    const std::wstring wide(url.begin(), url.end());
    ComPtr<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromURL(wide.c_str(), nullptr, &reader);
    out["create"] = static_cast<std::uint32_t>(hr);
    if (FAILED(hr)) return out;
    constexpr DWORD stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
    ComPtr<IMFMediaType> native;
    hr = reader->GetNativeMediaType(stream, 0, &native);
    out["native"] = static_cast<std::uint32_t>(hr);
    if (SUCCEEDED(hr)) {
        GUID subtype{}; UINT32 ch = 0, rate = 0, bits = 0;
        native->GetGUID(MF_MT_SUBTYPE, &subtype); native->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch);
        native->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate); native->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits);
        out["nativeSubtype"] = subtype.Data1; out["channels"] = ch; out["rate"] = rate; out["bits"] = bits;
    }
    for (const int width : {16, 0, 24, 32}) {
        ComPtr<IMFMediaType> type; MFCreateMediaType(&type);
        type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        type->SetGUID(MF_MT_SUBTYPE, width == 32 ? MFAudioFormat_Float : MFAudioFormat_PCM);
        if (width && width != 32) type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, width);
        out[std::string("set") + std::to_string(width)] = static_cast<std::uint32_t>(reader->SetCurrentMediaType(stream, nullptr, type.Get()));
    }
    PROPVARIANT value{};
    out["duration"] = static_cast<std::uint32_t>(reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &value));
    PropVariantClear(&value);
    return out;
}

// Status, type and length of the first bytes, via WinINet (URL not logged).
Json HttpStatus(const std::string& url) {
    Json out;
    const auto session = InternetOpenA("Mozilla/5.0", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
    if (!session) return {{"open", GetLastError()}};
    const auto request = InternetOpenUrlA(session, url.c_str(), "Range: bytes=0-15\r\n", static_cast<DWORD>(-1L),
        INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_COOKIES, 0);
    if (!request) { out["request"] = GetLastError(); InternetCloseHandle(session); return out; }
    char text[256]{}; DWORD size = sizeof(text);
    if (HttpQueryInfoA(request, HTTP_QUERY_STATUS_CODE, text, &size, nullptr)) out["status"] = text;
    size = sizeof(text); text[0] = 0;
    if (HttpQueryInfoA(request, HTTP_QUERY_CONTENT_TYPE, text, &size, nullptr)) out["type"] = text;
    size = sizeof(text); text[0] = 0;
    if (HttpQueryInfoA(request, HTTP_QUERY_CONTENT_RANGE, text, &size, nullptr)) out["range"] = text;
    InternetCloseHandle(request); InternetCloseHandle(session);
    return out;
}

void Note(const std::string& provider, const std::string& step, const Json& detail) {
    Json entry{{"provider", provider}, {"step", step}, {"result", Json::parse(Redact(detail.dump(-1, ' ', false, Json::error_handler_t::replace)))}};
    logFile << entry.dump(-1, ' ', false, Json::error_handler_t::replace) << '\n' << std::flush;
    std::cout << provider << " " << step << ": " << entry["result"].dump().substr(0, 300) << std::endl;
}
}  // namespace

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc < 2) { std::cerr << "usage: lume-music-diag-probe <output.jsonl> [search query]\n"; return 2; }
    logFile.open(argv[1], std::ios::binary | std::ios::trunc);
    const std::string query = argc > 2 ? argv[2] : u8"周杰伦";
    const bool playCheck = argc > 3 && std::string(argv[3]) == "--play";
    wchar_t local[32768]{};
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768)) return 2;
    const auto root = std::filesystem::path(local) / L"Lume" / L"music-accounts";
    const auth::Cancelled never = [] { return false; };
    // One engine for every load, as the music host process uses it.
    WindowsAudioEngine engine;
    for (const auto provider : {auth::Provider::QQ, auth::Provider::Netease, auth::Provider::Kugou}) {
        const std::string name(auth::Name(provider));
        Json secrets;
        try { secrets = auth::LoadCredentials(root, provider); } catch (...) {}
        if (!secrets.is_object() || secrets.empty()) { Note(name, "account", "signed-out"); continue; }
        std::function<void(const Json&)> collect = [&](const Json& value) {
            if (value.is_object()) {
                for (const auto& item : value.items()) {
                    if (item.key() == "memberships") continue;
                    const auto& field = item.value();
                    if (field.is_string() && field.get_ref<const std::string&>().size() >= 5)
                        sensitiveValues.push_back(field.get<std::string>());
                    else if (field.is_number_integer() && std::to_string(field.get<std::int64_t>()).size() >= 5)
                        sensitiveValues.push_back(std::to_string(field.get<std::int64_t>()));
                    else collect(field);
                }
            } else if (value.is_array()) for (const auto& item : value) collect(item);
        };
        collect(secrets);
        // Profile and memberships. The copy is discarded; nothing is saved.
        try {
            auto copy = secrets;
            const auto reply = provider == auth::Provider::QQ ? auth::RunQQ(auth::Operation::Refresh, copy, never, Exchange) :
                provider == auth::Provider::Netease ? auth::RunNetease(auth::Operation::Refresh, copy, never, Exchange) :
                auth::RunKugou(auth::Operation::Refresh, copy, never, Exchange);
            Json memberships = Json::array();
            for (const auto& m : reply.memberships)
                memberships.push_back({{"kind", m.kind}, {"label", m.label}, {"level", m.level}, {"active", m.active},
                    {"iconUrl", m.iconUrl}, {"expiresAt", m.expiresAt}});
            Note(name, "profile", {{"status", reply.status}, {"error", reply.error}, {"memberships", memberships}});
        } catch (const std::exception& error) { Note(name, "profile", {{"exception", error.what()}}); }
        const auto& credentials = secrets;
        try {
            auto home = online::Execute(provider, "home", {{"offset", 0}}, credentials, never, Exchange);
            Note(name, "home", {{"recommendations", home.value("recommendations", Json::array()).size()},
                {"playlists", home.value("playlists", Json::array()).size()}, {"tracks", home.value("tracks", Json::array()).size()}});
        } catch (const std::exception& error) { Note(name, "home", {{"exception", error.what()}}); }
        Json tracks = Json::array();
        try {
            auto result = online::Execute(provider, "search", {{"query", query}, {"offset", 0}}, credentials, never, Exchange);
            if (result.contains("tracks") && result["tracks"].is_array()) tracks = result["tracks"];
            Note(name, "search", {{"count", tracks.size()}});
        } catch (const std::exception& error) { Note(name, "search", {{"exception", error.what()}}); }
        std::size_t probed = 0;
        for (const auto& track : tracks) {
            if (probed++ >= (playCheck ? 1u : 3u) || !track.is_object() || !track.contains("id")) break;
            const auto id = track.value("id", std::string{});
            Note(name, "track", {{"id", id}, {"title", track.value("title", "")},
                {"vipRequired", track.value("vipRequired", false)}, {"membershipBadge", track.value("membershipBadge", "")}});
            for (const auto* quality : {"standard", "high", "lossless", "hires", "jyeffect", "sky", "jymaster"}) {
                if (!ValidAudioQuality(quality, name)) continue;
                try {
                    const auto result = online::Execute(provider, "resolve", {{"id", id}, {"quality", quality}},
                        credentials, never, Exchange);
                    const auto url = result.value("url", std::string{});
                    const auto file = url.substr(0, url.find('?'));
                    Json play = Json::object();
                    if (playCheck && !url.empty()) {
                        // Muted: no sound reaches the speakers. Load, play ~3 s,
                        // seek to 60 s, play again, and record what happened.
                        engine.SetMuted(true);
                        const auto started = std::chrono::steady_clock::now();
                        std::cerr << "load " << quality << std::endl;
                        const bool loaded = engine.Load(url);
                        std::cerr << "loaded " << loaded << std::endl;
                        const auto loadMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
                        play = {{"loaded", loaded}, {"loadMs", loadMs}, {"duration", engine.Duration()}};
                        if (!loaded) {
                            play["mf"] = DescribeSource(url);
                            play["http"] = HttpStatus(url);
                        }
                        if (loaded && engine.Play()) {
                            std::this_thread::sleep_for(std::chrono::seconds(3));
                            play["positionAfter3s"] = engine.Position();
                            play["playingAfter3s"] = engine.Playing();
                            const auto seekStart = std::chrono::steady_clock::now();
                            std::cerr << "seek" << std::endl;
                            const bool seeked = engine.Seek(60) && engine.Play();
                            double lastPosition = 0; long long worstControlMs = 0;
                            for (int i = 0; i < 20; ++i) {
                                const auto before = std::chrono::steady_clock::now();
                                lastPosition = engine.Position();
                                worstControlMs = std::max<long long>(worstControlMs, std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - before).count());
                                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                            }
                            play["seekAccepted"] = seeked;
                            play["positionAfterSeek"] = lastPosition;
                            play["playingAfterSeek"] = engine.Playing();
                            play["worstPositionCallMs"] = worstControlMs;
                            play["seekToSettleMs"] = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - seekStart).count();
                        }
                        std::cerr << "stop" << std::endl; engine.Stop(); std::cerr << "stopped" << std::endl;
                    }
                    Note(name, std::string("resolve ") + quality, {{"actualQuality", result.value("quality", "")}, {"play", play},
                        {"bitrate", result.value("bitrate", Json(0))},
                        {"file", file.substr(file.find_last_of('/') == std::string::npos ? 0 : file.find_last_of('/') + 1)},
                        {"host", url.substr(0, url.find('/', 8))}});
                } catch (const std::exception& error) {
                    Note(name, std::string("resolve ") + quality, {{"exception", error.what()}});
                }
            }
        }
    }
    std::cout << "log: " << argv[1] << '\n';
    return 0;
}
