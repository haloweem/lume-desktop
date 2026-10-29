// Standalone protocol fixture: link netease_account.cpp, not music_provider_crypto.cpp.
// Crypto spies make the wire envelope and RSA padding deterministic and observable.
#include "platform/windows/music_provider_http.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>

using namespace lume::music::auth;
namespace {
void Expect(bool ok, const char* why) {
    if (!ok) { std::cerr << why << '\n'; std::exit(1); }
}
struct Cipher { std::string input, key, iv; bool encrypt; };
std::vector<Cipher> ciphers;
std::vector<std::string> hashes;
std::string rsaInput;
std::string Decode(std::string_view hex) {
    Expect(hex.size() % 2 == 0, "hex envelope length");
    std::string out;
    for (std::size_t i = 0; i < hex.size(); i += 2)
        out += static_cast<char>(std::stoul(std::string(hex.substr(i, 2)), nullptr, 16));
    return out;
}
std::string Public(const Reply& r) {
    return Json{{"status", r.status}, {"phase", r.phase}, {"userId", r.userId},
        {"displayName", r.displayName}, {"qrText", r.qrText}, {"qrImage", r.qrImage}, {"error", r.error}}.dump();
}
HttpResponse Response(const Json& body, std::vector<std::string> cookies = {}) {
    HttpResponse r; r.status = 200; r.body = body.dump(); r.cookies = std::move(cookies); return r;
}
}
namespace lume::music::auth {
std::string RandomHex(std::size_t bytes) { return std::string(bytes * 2, 'a'); }
std::string HashHex(std::string_view input, std::string_view) {
    hashes.emplace_back(input); return std::string(32, 'd');
}
std::string Base64(std::string_view input) { return "b64[" + std::string(input) + "]"; }
std::string UrlEncode(std::string_view input) {
    std::string out;
    constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned char c : input) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}
std::string Aes(std::string_view input, std::string_view key, std::string_view iv, bool encrypt) {
    ciphers.push_back({std::string(input), std::string(key), std::string(iv), encrypt});
    return std::string(input);
}
std::string RsaEncrypt(std::string_view input, std::string_view pem, bool raw) {
    Expect(raw && pem.find("gtQn2JZ34") != pem.npos, "historical NetEase raw RSA key");
    rsaInput = input;
    Expect(input.size() == 128 && input.substr(0, 112) == std::string(112, '\0') &&
        input.substr(112) == std::string(16, 'a'), "NetEase left-padded reversed AES key");
    return std::string(128, 'r');
}
}
int main() {
    Json secrets = Json::object();
    int calls = 0, pollCode = 801;
    bool omitCookie = false, badProfile = false, revokedProfile = false, mismatchedProfile = false;
    bool cancel = false, cancelOnResponse = false, vipFails = false;
    int vipCalls = 0;
    Exchange http = [&](const HttpRequest& request, const Cancelled&) {
        ++calls;
        Expect(request.provider == Provider::Netease && request.method == "POST", "NetEase route and method");
        if (request.url.find("/eapi/") != std::string::npos) {
            Expect(request.url.find("https://interfacepc.music.163.com/eapi/") == 0, "historical eapi host");
            Expect(request.body.find("params=") == 0, "eapi form");
            const auto plain = Decode(std::string_view(request.body).substr(7));
            const std::string marker = "-36cd479b6b5-";
            const auto first = plain.find(marker);
            const auto last = plain.rfind(marker);
            const auto path = plain.substr(0, first);
            const auto text = plain.substr(first + marker.size(), last - first - marker.size());
            const auto payload = Json::parse(text);
            Expect(payload["type"] == 3 && payload["e_r"] == false, "QR type3 with unencrypted response");
            Expect(payload["header"]["os"] == "pc" && payload["header"]["appver"] == "3.1.17.204416",
                "historical desktop eapi headers");
            Expect(hashes.back() == "nobody" + path + "use" + text + "md5forencrypt" &&
                plain.substr(last + marker.size()) == std::string(32, 'd'), "exact eapi digest preimage and envelope");
            Expect(ciphers.back().key == "e82ckenh8dichen8" && ciphers.back().iv.empty(), "eapi AES ECB");
            if (path == "/api/login/qrcode/unikey")
                return Response({{"code", 200}, {"unikey", "fixture-key"}});
            Expect(path == "/api/login/qrcode/client/login" && payload["key"] == "fixture-key", "QR polling exact route/key");
            if (cancelOnResponse) cancel = true;
            return Response({{"code", pollCode}, {"token", "ignored-payload-secret"}},
                omitCookie ? std::vector<std::string>{} :
                std::vector<std::string>{"MUSIC_U=fixture-secret; Path=/; HttpOnly", "__csrf=fixture-csrf; Path=/"});
        }
        if (request.url == "https://music.163.com/weapi/music-vip-membership/client/vip/info") {
            ++vipCalls;
            if (vipFails) return Response({{"code", 500}});
            // Live shape (2026-10-11): 黑胶VIP active, no SVIP.
            return Response({{"code", 200}, {"data", {{"redVipLevel", 2},
                {"associator", {{"vipCode", 100}, {"expireTime", 4'102'444'800'000LL}}},
                {"redplus", {{"vipCode", 0}, {"expireTime", 0}}}}}});
        }
        Expect(request.url == "https://music.163.com/weapi/nuser/account/get" ||
            request.url == "https://music.163.com/weapi/w/nuser/account/get", "fixed account validation route");
        Expect(request.headers.at("Cookie").find("MUSIC_U=fixture-secret") != std::string::npos,
            "profile validates cookie through injected HTTP");
        Expect(request.body.find("params=") == 0 && request.body.find("&encSecKey=") != std::string::npos,
            "weapi encrypted form");
        const auto& inner = ciphers[ciphers.size() - 2];
        const auto& outer = ciphers.back();
        Expect(inner.key == "0CoJUm6Qyw8W8jud" && outer.key == std::string(16, 'a') &&
            inner.iv == "0102030405060708" && outer.iv == inner.iv, "double CBC encryption");
        const auto data = Json::parse(inner.input);
        Expect(data["csrf_token"] == "fixture-csrf" && data["e_r"] == false, "weapi CSRF and response flag");
        if (badProfile) return Response({{"code", 200}});
        if (revokedProfile) return Response({{"code", 200}, {"profile", nullptr}});
        return Response({{"code", 200}, {"account", {{"id", mismatchedProfile ? 42 : 7}, {"vipType", 11}}},
            {"profile", {{"userId", 7}, {"nickname", "Fixture"},
                {"avatarUrl", "http://p1.music.126.net/avatar.jpg"}}}});
    };
    auto reply = RunNetease(Operation::Begin, secrets, [&] { return cancel; }, http);
    if (reply.status != "authorizing")
        std::cerr << "begin status=" << reply.status << " error=" << reply.error
                  << " calls=" << calls << " ciphers=" << ciphers.size() << '\n';
    Expect(reply.status == "authorizing" && reply.phase == "waiting" &&
        reply.qrText == "https://music.163.com/login?codekey=fixture-key" && reply.qrImage.empty(),
        "begin emits only locally renderable QR text");
    Expect(secrets["qrKey"] == "fixture-key", "QR transaction native");
    reply = RunNetease(Operation::Poll, secrets, {}, http);
    Expect(reply.phase == "waiting" && !secrets.contains("MUSIC_U"), "waiting cannot import cookie");
    pollCode = 802;
    reply = RunNetease(Operation::Poll, secrets, {}, http);
    Expect(reply.phase == "scanned" && !secrets.contains("MUSIC_U"), "scanned not signed in");
    pollCode = 8821;
    reply = RunNetease(Operation::Poll, secrets, {}, http);
    Expect(reply.status == "error" && reply.error == "netease-code-8821" && !secrets.contains("MUSIC_U"),
        "a risk-check refusal reaches the page with its code");
    pollCode = 800;
    reply = RunNetease(Operation::Poll, secrets, {}, http);
    Expect(reply.status == "expired" && !secrets.contains("qrKey"), "expired clears transaction");
    const auto before = calls;
    RunNetease(Operation::Poll, secrets, {}, http);
    Expect(calls == before, "no polling an expired transaction");
    RunNetease(Operation::Begin, secrets, {}, http);
    pollCode = 803; omitCookie = true;
    reply = RunNetease(Operation::Poll, secrets, {}, http);
    Expect(reply.status == "error" && !secrets.contains("MUSIC_U"), "confirmed requires MUSIC_U");
    omitCookie = false; badProfile = true;
    reply = RunNetease(Operation::Poll, secrets, {}, http);
    Expect(reply.status == "error" && !secrets.contains("MUSIC_U"), "confirmed requires fetched profile");
    badProfile = false; mismatchedProfile = true;
    reply = RunNetease(Operation::Poll, secrets, {}, http);
    Expect(reply.status == "error" && !secrets.contains("MUSIC_U"), "profile/account ID mismatch");
    mismatchedProfile = false;
    reply = RunNetease(Operation::Poll, secrets, {}, http);
    Expect(reply.status == "signed-in" && reply.userId == "7" && reply.displayName == "Fixture" &&
        !secrets.contains("qrKey") && secrets["MUSIC_U"] == "fixture-secret", "confirmed validated account persisted");
    for (const auto* value : {"fixture-secret", "fixture-csrf", "MUSIC_U", "ignored-payload-secret"})
        Expect(Public(reply).find(value) == std::string::npos, "no credentials in public reply");
    Expect(reply.avatarUrl == "https://p1.music.126.net/avatar.jpg", "NetEase avatar is upgraded to https");
    Expect(reply.memberships.size() == 2 && reply.memberships[0].kind == "netease-vip" &&
        reply.memberships[0].active && reply.memberships[0].level == 2 &&
        reply.memberships[0].expiresAt == 4'102'444'800'000LL && reply.memberships[1].kind == "netease-svip" &&
        !reply.memberships[1].active, "NetEase 黑胶VIP comes from the membership service, SVIP stays inactive");
    const auto signedInCalls = calls;
    reply = RunNetease(Operation::Refresh, secrets, {}, http);
    Expect(reply.status == "signed-in" && calls == signedInCalls + 2, "restore validates persisted cookie");
    vipFails = true;
    reply = RunNetease(Operation::Refresh, secrets, {}, http);
    Expect(reply.status == "signed-in" && reply.memberships.size() == 2 && reply.memberships[0].active &&
        !reply.memberships[1].active, "failed membership service falls back to account vipType");
    vipFails = false;
    revokedProfile = true;
    reply = RunNetease(Operation::Refresh, secrets, {}, http);
    Expect(reply.status == "expired" && !secrets.contains("MUSIC_U"), "anonymous refresh is expired");
    revokedProfile = false;
    RunNetease(Operation::Begin, secrets, {}, http);
    cancelOnResponse = true;
    reply = RunNetease(Operation::Poll, secrets, [&] { return cancel; }, http);
    Expect(reply.status == "signed-out" && reply.error == "cancelled" &&
        !secrets.contains("MUSIC_U") && !secrets.contains("qrKey"), "cancelled late confirmation cannot commit");
    const auto cancelledCalls = calls;
    RunNetease(Operation::Begin, secrets, [] { return true; }, http);
    Expect(calls == cancelledCalls, "pre-cancelled makes no request");
    cancel = false;
    reply = RunNetease(Operation::Begin, secrets, {}, [](const HttpRequest&, const Cancelled&) -> HttpResponse {
        throw std::runtime_error("fixture-secret");
    });
    Expect(reply.error == "network" && Public(reply).find("fixture-secret") == std::string::npos,
        "exception details never escape");
    for (const auto& body : {std::string("not-json fixture-secret"), std::string("{}"), std::string(262145, 's')}) {
        reply = RunNetease(Operation::Begin, secrets, {}, [&](const HttpRequest&, const Cancelled&) {
            HttpResponse r; r.status = 200; r.body = body; return r;
        });
        Expect(reply.status == "error" && reply.error == "invalid-response", "invalid/oversized response rejected");
    }
    std::cout << "NetEase QR protocol fixtures passed without network\n";
}
