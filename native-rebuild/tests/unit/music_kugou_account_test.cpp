// Standalone protocol fixture: link kugou_account.cpp, not music_provider_crypto.cpp.
// Native crypto primitives have separate tests; these spies expose signed preimages.
#include "platform/windows/music_provider_http.h"
#include <cstdlib>
#include <iostream>

using namespace lume::music::auth;
namespace {
void Expect(bool ok, const char* why) {
    if (!ok) { std::cerr << why << '\n'; std::exit(1); }
}
std::vector<std::string> hashes;
std::string registrationPlain, registrationRsa, profileRsa;
// The risk service sometimes answers status 1 with an empty data object (live 2026-10-22).
bool emptyRegistration = false;
std::string Decode(std::string_view text) {
    std::string result;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%') {
            Expect(i + 2 < text.size(), "valid URL escape");
            result += static_cast<char>(std::stoul(std::string(text.substr(i + 1, 2)), nullptr, 16)); i += 2;
        } else result += text[i];
    }
    return result;
}
std::map<std::string, std::string> Query(const std::string& url) {
    std::map<std::string, std::string> out;
    auto start = url.find('?') + 1;
    while (start < url.size()) {
        const auto end = url.find('&', start);
        const auto pair = url.substr(start, end - start);
        const auto split = pair.find('=');
        out.emplace(pair.substr(0, split), Decode(pair.substr(split + 1)));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return out;
}
std::string Public(const Reply& r) {
    return Json{{"status", r.status}, {"phase", r.phase}, {"userId", r.userId},
        {"displayName", r.displayName}, {"qrText", r.qrText}, {"qrImage", r.qrImage}, {"error", r.error}}.dump();
}
HttpResponse Response(Json body) {
    HttpResponse r; r.status = 200; r.body = body.dump(); return r;
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
    Expect(key == std::string(16, 'd') && iv == std::string(16, 'd'), "registration MD5 split AES128 key/iv");
    if (encrypt) { registrationPlain = input; return "encrypted-registration"; }
    Expect(input == "binary-registration-response", "raw encrypted response passed to AES");
    return emptyRegistration ? R"({"status":1,"data":{}})" : R"({"status":1,"data":{"dfid":"fixture-dfid"}})";
}
std::string RsaEncrypt(std::string_view input, std::string_view pem, bool raw) {
    Expect(pem.find("DECi0Np2UR") != pem.npos, "lite-client RSA key");
    if (raw) {
        Expect(input.size() == 128, "profile raw RSA block width");
        const auto zero = input.find('\0');
        Expect(zero != input.npos && input.substr(zero) == std::string(128 - zero, '\0'), "Kugou right zero padding");
        profileRsa = input.substr(0, zero);
    } else registrationRsa = input;
    return std::string(128, 'r');
}
}
int main() {
    Json secrets = Json::object();
    int calls = 0, registers = 0, polls = 0, profiles = 0, qrStatus = 1;
    bool noToken = false, invalidProfile = false, mismatch = false, rejected = false, serviceFailure = false;
    bool verify = false, cancel = false, cancelOnResponse = false;
    Exchange http = [&](const HttpRequest& request, const Cancelled&) {
        ++calls;
        Expect(request.provider == Provider::Kugou, "Kugou provider");
        auto params = Query(request.url);
        Expect(params.at("clientver") == "11440" && params.at("uuid") == "-" &&
            request.headers.at("mid") == params.at("mid") &&
            request.headers.at("clienttime") == params.at("clienttime"), "lite identity/query/header parity");
        const bool web = request.url.find("https://login-user.kugou.com/") == 0;
        const std::string salt = web ? "NVPh5oo715z5DIWAeQlhMDsWXXQV4hwt" : "LnT6xpN3khm36zse0QzvmgTZ3waWdRSA";
        Expect(params.at("signature") == std::string(32, 'd'), "signature included");
        params.erase("signature");
        std::string expected = salt;
        for (const auto& p : params) expected += p.first + "=" + p.second;
        expected += (web ? "" : request.body) + salt;
        Expect(hashes.back() == expected, "exact sorted historical signature preimage");
        if (request.url.find("https://userservice.kugou.com/risk/v2/r_register_dev?") == 0) {
            ++registers;
            Expect(request.method == "POST" && request.body == "b64[encrypted-registration]" &&
                params.at("part") == "1" && params.at("platid") == "1" && params.at("appid") == "3116",
                "encrypted registration body and params");
            const auto data = Json::parse(registrationPlain);
            Json expectedData = {
                {"availableRamSize", 4983533568LL}, {"availableRomSize", 48114719},
                {"availableSDSize", 48114717}, {"basebandVer", ""}, {"batteryLevel", 100},
                {"batteryStatus", 3}, {"brand", "Redmi"}, {"buildSerial", "unknown"},
                {"device", "marble"}, {"imei", "AAAAAAAAAAAA4AAAAAAAAAAAAAAAAAAA"},
                {"imsi", ""}, {"manufacturer", "Xiaomi"}, {"uuid", "AAAAAAAAAAAA4AAAAAAAAAAAAAAAAAAA"},
                {"accelerometer", false}, {"accelerometerValue", ""},
                {"gravity", false}, {"gravityValue", ""}, {"gyroscope", false}, {"gyroscopeValue", ""},
                {"light", false}, {"lightValue", ""}, {"magnetic", false}, {"magneticValue", ""},
                {"orientation", false}, {"orientationValue", ""}, {"pressure", false}, {"pressureValue", ""},
                {"step_counter", false}, {"step_counterValue", ""}, {"temperature", false}, {"temperatureValue", ""}
            };
            Expect(data == expectedData, "all 31 historical registration fields and UUID identity");
            Expect(Json::parse(registrationRsa)["aes"] == "aaaaaa", "registration RSA carries AES seed");
            HttpResponse r; r.status = 200; r.body = "binary-registration-response"; return r;
        }
        Expect(params.at("dfid") == (emptyRegistration ? "-" : "fixture-dfid"), "device registered before login");
        if (request.url.find("https://login-user.kugou.com/v2/qrcode?") == 0) {
            Expect(request.method == "GET" && params.at("appid") == "1001" &&
                params.at("srcappid") == "2919" && params.at("type") == "1" && params.at("plat") == "4" &&
                params.at("qrcode_txt") == "https://h5.kugou.com/apps/loginQRCode/html/index.html?appid=3116&",
                "QR key historical parameters");
            return Response({{"status", 1}, {"data", {{"qrcode", "fixture-key"}}}});
        }
        if (request.url.find("https://login-user.kugou.com/v2/get_userinfo_qrcode?") == 0) {
            ++polls;
            Expect(request.method == "GET" && params.at("appid") == "3116" &&
                params.at("qrcode") == "fixture-key", "poll route and lite appid");
            if (verify) { verify = false; return Response({{"status", 0}, {"errcode", 20028}}); }
            if (cancelOnResponse) cancel = true;
            Json data = {{"status", qrStatus}, {"userid", 7}, {"nickname", "Not trusted"}};
            if (!noToken) data["token"] = "fixture-secret";
            return Response({{"status", 1}, {"data", data}});
        }
        Expect(request.url.find("https://gateway.kugou.com/v3/get_my_info?") == 0,
            "only profile route, no membership claim or youth VIP calls");
        ++profiles;
        Expect(request.method == "POST" && request.headers.at("x-router") == "usercenter.kugou.com" &&
            params.at("token") == "fixture-secret" && params.at("userid") == "7", "authenticated profile request");
        const auto body = Json::parse(request.body);
        const auto auth = Json::parse(profileRsa);
        Expect(body["userid"] == 7 && body["usertype"] == 1 &&
            body["visit_time"] == auth["clienttime"] && auth["token"] == "fixture-secret",
            "profile token RSA and timestamps");
        if (rejected) { HttpResponse r; r.status = 401; return r; }
        if (serviceFailure) return Response({{"status", 0}, {"error_code", 500}});
        if (invalidProfile) return Response({{"status", 1}, {"data", Json::object()}});
        return Response({{"status", 1}, {"data", {{"nickname", "Fixture"}, {"userid", mismatch ? 42 : 7}}}});
    };
    auto reply = RunKugou(Operation::Begin, secrets, {}, http);
    Expect(reply.status == "authorizing" && reply.phase == "waiting" && registers == 1 &&
        reply.qrText == "https://h5.kugou.com/apps/loginQRCode/html/index.html?qrcode=fixture-key",
        "registration then local QR text");
    const auto device = secrets["KUGOU_API_GUID"];
    Expect(secrets["KUGOU_API_MID"] == "294911384664813335001591326440865783261", "128-bit MD5 converted to decimal MID");
    reply = RunKugou(Operation::Poll, secrets, {}, http);
    Expect(reply.phase == "waiting" && !secrets.contains("token") && profiles == 0, "waiting token ignored");
    for (int state : {2, 3}) {
        qrStatus = state;
        reply = RunKugou(Operation::Poll, secrets, {}, http);
        Expect(reply.phase == "scanned" && !secrets.contains("token") && profiles == 0, "scanned never authenticates");
    }
    qrStatus = 0;
    reply = RunKugou(Operation::Poll, secrets, {}, http);
    Expect(reply.status == "expired" && !secrets.contains("qrKey") && secrets["KUGOU_API_GUID"] == device,
        "expiry preserves device only");
    RunKugou(Operation::Begin, secrets, {}, http);
    Expect(registers == 1, "stable dfid avoids repeat registration");
    qrStatus = 4; noToken = true;
    reply = RunKugou(Operation::Poll, secrets, {}, http);
    Expect(reply.status == "error" && profiles == 0 && !secrets.contains("token"), "confirmed requires token");
    noToken = false; invalidProfile = true;
    reply = RunKugou(Operation::Poll, secrets, {}, http);
    Expect(reply.status == "error" && !secrets.contains("token"), "profile required before commit");
    invalidProfile = false; mismatch = true;
    reply = RunKugou(Operation::Poll, secrets, {}, http);
    Expect(reply.status == "error" && !secrets.contains("token"), "mismatched user rejected");
    mismatch = false; verify = true;
    const auto beforePolls = polls;
    reply = RunKugou(Operation::Poll, secrets, {}, http);
    Expect(reply.status == "signed-in" && reply.userId == "7" && reply.displayName == "Fixture" &&
        registers == 2 && polls == beforePolls + 2 && !secrets.contains("qrKey"), "verification re-registers and retries once");
    for (const auto* value : {"fixture-secret", "fixture-dfid", "KUGOU_API", "Not trusted"})
        Expect(Public(reply).find(value) == std::string::npos, "public result contains no device/auth secrets");
    const auto beforeProfiles = profiles;
    reply = RunKugou(Operation::Refresh, secrets, {}, http);
    Expect(reply.status == "signed-in" && profiles == beforeProfiles + 1, "refresh validates persisted credentials");
    serviceFailure = true;
    reply = RunKugou(Operation::Refresh, secrets, {}, http);
    Expect(reply.status == "error" && reply.error == "service" && secrets.contains("token"),
        "generic service rejection is not proof of credential expiry");
    serviceFailure = false;
    rejected = true;
    reply = RunKugou(Operation::Refresh, secrets, {}, http);
    Expect(reply.status == "expired" && !secrets.contains("token") && !secrets.contains("userid"),
        "invalid persisted credentials expired");
    rejected = false;
    RunKugou(Operation::Begin, secrets, {}, http);
    cancelOnResponse = true;
    reply = RunKugou(Operation::Poll, secrets, [&] { return cancel; }, http);
    Expect(reply.status == "signed-out" && reply.error == "cancelled" &&
        !secrets.contains("token") && !secrets.contains("qrKey"), "late cancelled response cannot commit credentials");
    const auto beforeCalls = calls;
    RunKugou(Operation::Begin, secrets, [] { return true; }, http);
    Expect(calls == beforeCalls, "pre-cancelled does not send");
    reply = RunKugou(Operation::Begin, secrets, {}, [](const HttpRequest&, const Cancelled&) -> HttpResponse {
        throw std::runtime_error("fixture-secret");
    });
    Expect(reply.error == "network" && Public(reply).find("fixture-secret") == std::string::npos, "sanitized network errors");
    for (const auto& body : {std::string("fixture-secret"), std::string("{}"), std::string(262145, 's')}) {
        reply = RunKugou(Operation::Begin, secrets, {}, [&](const HttpRequest&, const Cancelled&) {
            HttpResponse r; r.status = 200; r.body = body; return r;
        });
        Expect(reply.status == "error", "malformed response rejected");
    }
    // A failed QR fetch still keeps the device it minted.
    Json fresh = Json::object();
    RunKugou(Operation::Begin, fresh, {}, [](const HttpRequest&, const Cancelled&) -> HttpResponse {
        throw std::runtime_error("offline");
    });
    Expect(fresh.contains("KUGOU_API_GUID") && fresh.contains("KUGOU_API_MID") && !fresh.contains("qrKey"),
        "failed attempt keeps the device only");
    // Registration that never returns a dfid must not block the QR code.
    emptyRegistration = true;
    Json unregistered = Json::object();
    auto beforeRegisters = registers;
    reply = RunKugou(Operation::Begin, unregistered, {}, http);
    Expect(reply.status == "authorizing" && !reply.qrText.empty() && registers == beforeRegisters + 2 &&
        !unregistered.contains("dfid") && unregistered.contains("dfidTriedAt"),
        "QR is issued with dfid '-' after two empty registrations");
    beforeRegisters = registers;
    qrStatus = 1;
    reply = RunKugou(Operation::Poll, unregistered, {}, http);
    Expect(reply.phase == "waiting" && registers == beforeRegisters, "polling does not re-register during the pause");
    emptyRegistration = false;
    unregistered["dfidTriedAt"] = "1000000000";
    reply = RunKugou(Operation::Poll, unregistered, {}, http);
    Expect(reply.phase == "waiting" && registers == beforeRegisters + 1 &&
        unregistered["dfid"] == "fixture-dfid" && !unregistered.contains("dfidTriedAt"),
        "registration is retried after the pause and keeps the dfid");
    std::cout << "Kugou QR protocol fixtures passed without network\n";
}
