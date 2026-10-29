#include "platform/windows/music_provider_http.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace lume::music::auth;
bool passed = true;
void Check(bool value, const char* message) {
    if (!value) {
        passed = false;
        std::cerr << "FAIL: " << message << '\n';
    }
}
// A failed attempt may keep the device it presented and must change nothing else.
bool SameExceptDevice(Json after, Json before) {
    if (after.is_object()) after.erase("device");
    if (before.is_object()) before.erase("device");
    return after == before && !(after.is_object() && after.contains("credential") &&
        (!before.is_object() || after["credential"] != before.value("credential", Json{})));
}
constexpr char Ticket[] = "fake-qr-ticket-0123456789abcdefghijklmnopqrstuvwxyz-0123456789abcdef";
constexpr char Callback[] =
    "https://ssl.ptlogin2.graph.qq.com/check_sig?uin=123456&ptsigx=sig%2Bx%3D&s_url=x";
constexpr char Redirect[] =
    "https://y.qq.com/portal/wx_redirect.html?code=oauth%2Bcode%3D&state=state";

std::string Png() {
    // Fixed 1x1 PNG fixture; never a real QR challenge or account.
    constexpr unsigned char bytes[] = {
        137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,
        31,21,196,137,0,0,0,11,73,68,65,84,120,156,99,96,0,2,0,0,5,0,1,
        165,246,69,64,0,0,0,0,73,69,78,68,174,66,96,130
    };
    return {reinterpret_cast<const char*>(bytes), sizeof(bytes)};
}

HttpResponse JsonResponse(const Json& value) {
    return {200, value.dump(), "application/json", {}, {}};
}
HttpResponse MusicData(const Json& data) {
    return JsonResponse({{"req_0", {{"code", 0}, {"data", data}}}});
}
Json SeedAttempt() {
    return {{"attempt", {{"id", "fake-attempt"}, {"qrsig", Ticket}}}};
}

void Public(const Reply& reply) {
    const std::vector<std::string> statuses{"signed-out", "authorizing", "signed-in", "expired", "error"};
    const std::vector<std::string> errors{"", "network", "service", "invalid-response", "expired",
        "qq-status-response", "qq-authorization-response", "qq-device-response", "qq-credential-response", "qq-profile-response",
        "qq-credential-network", "qq-credential-malformed", "qq-credential-missing-id",
        "qq-credential-missing-key", "qq-credential-expired", "qq-credential-http-503",
        "qq-credential-code-104009", "qq-credential-code-12345"};
    Check(std::find(statuses.begin(), statuses.end(), reply.status) != statuses.end(), "public status contract");
    Check(reply.phase.empty() || reply.phase == "waiting" || reply.phase == "scanned", "public phase contract");
    Check(std::find(errors.begin(), errors.end(), reply.error) != errors.end(), "sanitized public error");
    Check(reply.qrImage.size() <= 350000, "public PNG size contract");
    Check(PublicImageUrl(reply.avatarUrl) && reply.memberships.size() <= 8, "public avatar and membership limits");
    auto text = reply.status + reply.phase + reply.userId + reply.displayName +
        reply.error + reply.qrText + reply.qrImage + reply.avatarUrl;
    for (const auto& membership : reply.memberships) {
        Check(PublicImageUrl(membership.iconUrl) && membership.level >= 0 && membership.level <= 1000,
              "public membership image and level contract");
        text += membership.kind + membership.label + membership.iconUrl;
    }
    for (const auto* secret : {Ticket, "fake-skey", "music-key", "refresh-token", "refresh-key",
                              "oauth+code=", "device-sid", "open-id"})
        Check(text.find(secret) == std::string::npos, "public reply contains no native secrets");
}

struct FakeQQ {
    int pollCode = 0;
    std::uint32_t checkStatus = 302;
    std::uint32_t authorizeStatus = 302;
    std::string callback = Callback, location = Redirect;
    std::vector<std::string> steps;
    std::vector<HttpRequest> requests;
    std::function<void(const std::string&, HttpResponse&)> alter;
    std::function<void(const std::string&)> after;

    Exchange Transport() {
        return [this](const HttpRequest& request, const Cancelled&) { return Send(request); };
    }
    HttpResponse Send(const HttpRequest& request) {
        requests.push_back(request);
        Check(request.provider == Provider::QQ, "only QQ provider HTTP");
        const auto path = request.url.substr(0, request.url.find('?'));
        std::string step;
        HttpResponse response;
        if (path == "https://ssl.ptlogin2.qq.com/ptqrshow") {
            step = "qr";
            Check(request.method == "GET" && request.body.empty(), "QR uses GET");
            Check(request.maxResponse <= 256 * 1024, "QR response is bounded to 256KiB");
            Check(request.url.find("appid=716027609&e=2&l=M&s=3&d=72&v=4&t=0.") != std::string::npos &&
                  request.url.find("&daid=383&pt_3rd_aid=100497308") != std::string::npos, "exact QR parameters");
            Check(request.headers.at("referer") == "https://xui.ptlogin2.qq.com/", "QR referer");
            response = {200, Png(), "image/png", {std::string("qrsig=") + Ticket + "; Path=/; Secure"}, {}};
        } else if (path == "https://ssl.ptlogin2.qq.com/ptqrlogin") {
            step = "poll";
            Check(request.method == "GET" && request.body.empty(), "poll uses GET");
            Check(request.headers.at("cookie") == std::string("qrsig=") + Ticket, "poll private ticket");
            Check(request.url.find("ptqrtoken=2142345760") != std::string::npos, "long-ticket hash33 wraps at 32 bits");
            Check(request.url.find("&js_ver=20102616&js_type=1&pt_uistyle=40&aid=716027609") != std::string::npos &&
                  request.url.find("&has_onekey=1") != std::string::npos, "exact poll parameters");
            response = {200, "ptuiCB('" + std::to_string(pollCode) + "','0','" +
                (pollCode == 0 ? callback : "") + "','0','ok','nick');", "text/plain", {}, {}};
        } else if (path == "https://ssl.ptlogin2.graph.qq.com/check_sig") {
            step = "check";
            Check(request.method == "GET", "check_sig uses GET");
            Check(request.url.find("uin=123456&pttype=1&service=ptqrlogin&nodirect=0&ptsigx=sig%2Bx%3D") !=
                  std::string::npos, "callback values decoded and encoded exactly once");
            Check(request.url.find("&pt_login_type=3&pt_aid=0&pt_aaid=16&pt_light=0&pt_3rd_aid=100497308") !=
                  std::string::npos, "exact check_sig parameters");
            response = {checkStatus, {}, {}, {"p_skey=fake-skey; Domain=.graph.qq.com; Path=/",
                "p_uin=o123456; Path=/"}, "https://graph.qq.com/oauth2.0/login_jump"};
        } else if (path == "https://graph.qq.com/oauth2.0/authorize") {
            step = "authorize";
            Check(request.method == "POST", "authorize uses POST");
            Check(request.headers.at("cookie") == "p_skey=fake-skey; p_uin=o123456", "only Cookie pairs forwarded");
            Check(request.body.find("&g_tk=2084755813&auth_time=") != std::string::npos, "OAuth hash33 seed 5381");
            Check(request.body.find("response_type=code&client_id=100497308&redirect_uri=" +
                UrlEncode("https://y.qq.com/portal/wx_redirect.html?login_type=1&surl=https://y.qq.com/")) == 0,
                "exact OAuth redirect target");
            const auto ui = request.body.substr(request.body.find("&ui=") + 4);
            Check(ui.size() == 36 && ui[14] == '4', "OAuth UUID");
            response = {authorizeStatus, {}, {}, {}, location};
        } else if (path == "https://api.tencentmusic.com/tme/trpc/proxy") {
            step = "qimei";
            Check(request.method == "POST", "QIMEI uses POST");
            Check(request.headers.at("method") == "GetQimei" &&
                  request.headers.at("service") == "trpc.tme_datasvr.qimeiproxy.QimeiProxy" &&
                  request.headers.at("appid") == "qimei_qq_android" &&
                  request.headers.at("Host") == "api.tencentmusic.com", "QIMEI dedicated service headers");
            const auto timestamp = request.headers.at("timestamp");
            Check(request.headers.at("sign") ==
                  HashHex("qimei_qq_androidpzAuCmaFAaFaHrdakPjLIEqKrGnSOOvH" + timestamp), "QIMEI header signature");
            const auto body = Json::parse(request.body);
            const auto params = body.at("qimeiParams");
            Check(body.at("app") == 0 && body.at("os") == 1 && params.at("time") == timestamp, "QIMEI envelope");
            const auto key = params.at("key").get<std::string>();
            const auto encrypted = params.at("params").get<std::string>();
            Check(key.size() == 172 && encrypted.size() > 100, "QIMEI RSA1024 key and AES payload");
            Check(params.at("nonce").get<std::string>().size() == 16, "QIMEI nonce");
            const auto extra = params.at("extra").get<std::string>();
            Check(extra == R"({"appKey":"0AND0HD6FE4HY80F"})", "QIMEI extra");
            Check(params.at("sign") == HashHex(key + encrypted +
                std::to_string(std::stoll(timestamp) * 1000) + params.at("nonce").get<std::string>() +
                "ZdJqM15EeO2zWc08" + extra), "QIMEI payload signature");
            Check(request.body.find("androidId") == std::string::npos, "device payload encrypted");
            response = JsonResponse({{"data", R"({"data":{"q16":"fake-q16","q36":"fake-q36"}})"}});
        } else if (path == "https://u.y.qq.com/cgi-bin/musicu.fcg") {
            Check(request.method == "POST", "musicu uses POST");
            Check(request.headers.at("user-agent") == "QQMusic 14090008(android 10)" &&
                  request.headers.at("referer") == "https://y.qq.com/", "music mobile headers");
            const auto body = Json::parse(request.body);
            const auto comm = body.at("comm");
            Check(comm.at("ct") == 11 && comm.at("cv") == 14090008 && comm.at("v") == 14090008 &&
                  comm.at("tmeAppID") == "qqmusic" && comm.at("tmeLoginType") == 2, "mobile comm");
            Check(comm.at("QIMEI") == "fake-q16" && comm.at("QIMEI36") == "fake-q36", "QIMEI bound to music requests");
            Check(comm.at("OpenUDID") == comm.at("udid") && comm.at("OpenUDID") == comm.at("OpenUDID2"),
                  "stable device identifiers");
            const auto req = body.at(body.contains("req_0") ? "req_0" : "req_1");
            const auto module = req.at("module").get<std::string>();
            if (module == "music.getSession.session") {
                step = "session";
                Check(req.at("method") == "GetSession" &&
                      req.at("param") == Json{{"uid", ""}, {"vkey", 0}, {"caller", 0}}, "exact GetSession");
                Check(!request.headers.count("cookie"), "anonymous device bootstrap");
                response = MusicData({{"session", {{"uid", 777}, {"sid", "device-sid"}, {"vkey", "device-vkey"}}}});
            } else if (module == "QQConnectLogin.LoginServer") {
                step = "login";
                Check(req.at("method") == "QQLogin" && req.at("param") == Json{{"code", "oauth+code="}},
                      "QQLogin receives decoded OAuth code only");
                Check(comm.at("uid") == "777" && comm.at("sid") == "device-sid", "QQLogin has bootstrapped session");
                response = MusicData({
                    {"musicid", 123456}, {"musickey", "music-key"}, {"refresh_token", "refresh-token"},
                    {"refresh_key", "refresh-key"}, {"openid", "open-id"}, {"keyExpiresIn", 7200}});
            } else if (module == "music.login.LoginServer") {
                step = "refresh";
                const auto param = req.at("param");
                Check(req.at("method") == "Login" && param.at("musicid") == 123456 &&
                      param.at("str_musicid") == "123456" && param.at("loginMode") == 2 &&
                      param.at("refresh_token") == "refresh-token" && param.at("refresh_key") == "refresh-key" &&
                      param.at("musickey") == "music-key" && param.at("openid") == "open-id", "exact token refresh");
                response = MusicData({
                    {"str_musicid", "123456"}, {"musickey", "renewed-music-key"}, {"keyExpiresIn", 7200}});
            } else if (module == "userInfo.BaseUserInfoServer") {
                step = "profile";
                Check(req.at("method") == "get_user_baseinfo_v2" &&
                      req.at("param").at("vec_uin") == Json::array({"123456"}) &&
                      !body.contains("req_0"), "base profile request is independent of optional VIP");
                const auto key = comm.at("authst").get<std::string>();
                Check(comm.at("qq") == "123456" && request.headers.at("cookie") ==
                      "uin=123456; qqmusic_uin=123456; qm_keyst=" + key + "; qqmusic_key=" + key,
                      "profile uses final music credential aliases");
                response = JsonResponse(Json::parse(R"({
                    "req_1":{"code":0,"data":{"map_userinfo":{"123456":{"base":{
                        "nick":"Test QQ","uin":123456,"headurl":"http://q1.qlogo.cn/bju.jpg"
                    }}}}}
                })"));
            } else if (module == "userInfo.VipQueryServer") {
                step = "vip";
                Check(req.at("method") == "SRFVipQuery_V2" &&
                      req.at("param").at("uin_list") == Json::array({"123456"}) &&
                      !body.contains("req_1"), "exact optional VIP request");
                Check(!steps.empty() && steps.back() == "profile", "basic identity succeeds before the VIP request");
                const auto key = comm.at("authst").get<std::string>();
                Check(comm.at("qq") == "123456" && request.headers.at("cookie") ==
                      "uin=123456; qqmusic_uin=123456; qm_keyst=" + key + "; qqmusic_key=" + key,
                      "VIP uses the authenticated profile credential");
                response = MusicData(Json::parse(R"({"infoMap":{"123456":{"vip":{
                    "isVip":1,"vipLevel":5,"vipName":"\u7eff\u94bb",
                    "isSuperVip":0,"vipEnd":2000000000
                }}}})"));
            } else if (module == "music.lvz.VipIconUiShowSvr") {
                step = "icons";
                Check(req.at("method") == "GetVipIconUiV2" && req.at("param") == Json{{"PID", 5}},
                      "exact VIP icon request");
                const auto key = comm.at("authst").get<std::string>();
                Check(comm.at("qq") == "123456" && request.headers.at("cookie") ==
                      "uin=123456; qqmusic_uin=123456; qm_keyst=" + key + "; qqmusic_key=" + key,
                      "VIP icons use the authenticated profile credential");
                response = MusicData({{"UserInfoUI", {{"iconlist", Json::array({
                    {{"desc", "svip"}, {"srcUrl", "http://y.qq.com/mediastyle/lv-icon/v14/2x/d-svip5.png"}},
                    {{"desc", "vip"}, {"srcUrl", "http://y.qq.com/mediastyle/lv-icon/v14/2x/vip5.png"}}
                })}}}});
            } else Check(false, "unexpected music module");
        } else Check(false, "unexpected HTTP host/path (no redirects, windows or fallback)");
        steps.push_back(step);
        if (alter) alter(step, response);
        if (after) after(step);
        return response;
    }
};

Reply Run(FakeQQ& fake, Operation operation, Json& secrets, const Cancelled& cancelled = {}) {
    auto reply = RunQQ(operation, secrets, cancelled, fake.Transport());
    Public(reply);
    return reply;
}
}

int main() {
    FakeQQ fake;
    Json secrets;
    auto reply = Run(fake, Operation::Begin, secrets);
    Check(reply.status == "authorizing" && reply.phase == "waiting" &&
          reply.qrImage == "data:image/png;base64," + Base64(Png()), "begin PNG data URI");
    Check(secrets.at("attempt").at("qrsig") == Ticket, "QR ticket stays native");
    fake.pollCode = 66;
    Check(Run(fake, Operation::Poll, secrets).phase == "waiting", "waiting state");
    fake.pollCode = 67;
    Check(Run(fake, Operation::Poll, secrets).phase == "scanned", "scanned state");
    fake.pollCode = 0;
    reply = Run(fake, Operation::Poll, secrets);
    Check(reply.status == "signed-in" && reply.userId == "123456" && reply.displayName == "Test QQ", "valid signed-in profile");
    Check(reply.avatarUrl == "https://q1.qlogo.cn/bju.jpg" && reply.memberships.size() == 2,
          "QQ avatar and separate green/super memberships reach Reply");
    if (reply.memberships.size() == 2) {
        const auto& green = reply.memberships[0];
        const auto& super = reply.memberships[1];
        Check(green.kind == "qq-green-vip" && green.label == u8"\u7eff\u94bb" &&
              green.level == 5 && green.active && green.expiresAt == 2'000'000'000'000LL &&
              green.iconUrl == "https://y.qq.com/mediastyle/lv-icon/v14/2x/vip5.png",
              "all six green membership fields are normalized");
        Check(super.kind == "qq-super-vip" && super.label == u8"\u8d85\u7ea7\u4f1a\u5458" &&
              super.level == 0 && !super.active && super.expiresAt == 0 &&
              super.iconUrl == "https://y.qq.com/mediastyle/lv-icon/v14/2x/d-svip5.png",
              "super membership has independent activity and icon");
    }
    Check(secrets.at("profile").at("avatarUrl") == reply.avatarUrl &&
          secrets.at("profile").at("memberships").size() == 2,
          "native cached profile retains public avatar and memberships");
    Check(fake.steps == std::vector<std::string>{"qr", "poll", "poll", "poll", "check", "authorize",
          "qimei", "session", "login", "profile", "vip", "icons"}, "complete native QR chain");
    Check(!secrets.contains("attempt") && secrets.dump().find("oauth+code=") == std::string::npos &&
          secrets.dump().find("fake-skey") == std::string::npos, "success discards all temporary authorization material");
    Check(secrets.at("credential").at("uin") == "123456" &&
          secrets.at("credential").at("updatedAt").get<std::int64_t>() > 0, "numeric musicid normalized and dated");
    {
        FakeQQ successWithOkStatus;
        successWithOkStatus.authorizeStatus = 200;
        auto attempt = SeedAttempt();
        Check(Run(successWithOkStatus, Operation::Poll, attempt).status == "signed-in",
              "OAuth Location is accepted when the authorization service returns HTTP 200");
    }
    for (const auto status : {200u, 301u, 307u, 308u}) {
        FakeQQ successWithRedirectVariant;
        successWithRedirectVariant.checkStatus = status;
        successWithRedirectVariant.authorizeStatus = status;
        auto attempt = SeedAttempt();
        Check(Run(successWithRedirectVariant, Operation::Poll, attempt).status == "signed-in",
              "QQ authorization accepts valid 2xx/3xx response variants");
    }
    {
        FakeQQ callbackWithoutState;
        callbackWithoutState.location = "https://y.qq.com/portal/wx_redirect.html?code=oauth%2Bcode%3D";
        auto attempt = SeedAttempt();
        Check(Run(callbackWithoutState, Operation::Poll, attempt).status == "signed-in",
              "OAuth callback code does not require a fixed state parameter");
    }
    if (!passed) return 1;
    const Json saved = secrets;

    for (int variant = 0; variant < 3; ++variant) {
        FakeQQ unavailable;
        unavailable.alter = [variant](const std::string& step, HttpResponse& response) {
            if (step != "profile") return;
            if (variant == 0) response.status = 503;
            if (variant == 1) throw std::runtime_error("private profile transport diagnostic");
            if (variant == 2) response = JsonResponse({{"req_1", {{"code", 500}}}});
        };
        auto attempt = SeedAttempt();
        const auto result = Run(unavailable, Operation::Poll, attempt);
        Check(result.status == "signed-in" && result.error.empty() &&
            result.userId == "123456" && result.displayName == "123456",
            "a temporary profile outage cannot reject a verified QQ login");
        Check(attempt.contains("credential") && !attempt.contains("attempt") &&
            attempt["credential"]["uin"] == "123456",
            "verified credential is committed before the consumed OAuth code is discarded");
    }
    for (const auto* stage : {"check", "authorize", "qimei", "session", "login"}) {
        FakeQQ unavailable;
        unavailable.alter = [stage](const std::string& step, HttpResponse& response) {
            if (step == stage) response.status = 503;
        };
        auto attempt = SeedAttempt();
        const auto before = attempt;
        const auto result = Run(unavailable, Operation::Poll, attempt);
        const auto expectedError = std::string(stage) == "check" || std::string(stage) == "authorize"
            ? "qq-authorization-response"
            : std::string(stage) == "qimei" || std::string(stage) == "session"
                ? "qq-device-response"
                : "qq-credential-http-503";
        // Only the device survives, so a retry presents the one QQ may already have seen.
        auto rest = attempt;
        rest.erase("device");
        auto expected = before;
        expected.erase("device");
        Check(result.status == "error" && result.error == expectedError &&
            rest == expected && !attempt.contains("credential"),
            "failed core authorization retains its stage and cannot commit a credential");
        if (std::string(stage) != "check" && std::string(stage) != "authorize")
            Check(attempt.contains("device") && attempt["device"].value("openUdid", "") != "",
                "a failed attempt keeps the device it already presented");
    }
    if (!passed) return 1;
    for (const auto [field, expected] : std::vector<std::pair<std::string, std::string>>{
        {"outer", "qq-credential-code-12345"}, {"request", "qq-credential-code-104009"},
        {"data", "qq-credential-code-104009"}, {"missing-id", "qq-credential-missing-id"},
        {"missing-key", "qq-credential-missing-key"}, {"expired", "qq-credential-expired"},
        {"malformed", "qq-credential-malformed"}, {"network", "qq-credential-network"}}) {
        FakeQQ failed;
        failed.alter = [field](const std::string& step, HttpResponse& response) {
            if (step != "login") return;
            if (field == "network") throw std::runtime_error("private music-key and account");
            auto payload = Json::parse(response.body);
            if (field == "outer") payload["code"] = 12345;
            else if (field == "request") payload["req_0"]["code"] = 104009;
            else if (field == "data") payload["req_0"]["data"]["code"] = 104009;
            else if (field == "missing-id") payload["req_0"]["data"].erase("musicid");
            else if (field == "missing-key") payload["req_0"]["data"].erase("musickey");
            else if (field == "expired") payload["req_0"]["data"]["expired_at"] = 1000000001;
            else if (field == "malformed") payload = Json::object();
            response = JsonResponse(payload);
        };
        auto attempt = SeedAttempt();
        const auto before = attempt;
        const auto result = Run(failed, Operation::Poll, attempt);
        // A rejected exchange (20279 and friends) keeps the device it presented.
        auto rest = attempt;
        rest.erase("device");
        auto expectedRest = before;
        expectedRest.erase("device");
        Check(result.error == expected && rest == expectedRest && !attempt.contains("credential") &&
            attempt.contains("device"), "credential diagnostic is bounded and does not commit secrets");
    }
    if (!passed) return 1;

    {
        FakeQQ optional;
        optional.alter = [](const std::string& step, HttpResponse& response) {
            if (step != "profile" && step != "vip") return;
            auto json = Json::parse(response.body);
            if (step == "vip") json["req_0"]["data"]["infoMap"] = Json::object();
            else json["req_1"]["data"]["map_userinfo"]["123456"]["base"].erase("headurl");
            response = JsonResponse(json);
        };
        auto stored = saved;
        const auto profile = Run(optional, Operation::Refresh, stored);
        Check(profile.status == "signed-in" && profile.memberships.empty() &&
              profile.avatarUrl == "https://q1.qlogo.cn/g?b=qq&nk=123456&s=100" &&
              optional.steps == std::vector<std::string>{"profile", "vip"},
              "missing optional VIP/avatar fields use the authenticated QQ avatar without inventing membership");
    }
    {
        FakeQQ aliases;
        aliases.alter = [](const std::string& step, HttpResponse& response) {
            if (step != "profile" && step != "vip") return;
            auto json = Json::parse(response.body);
            if (step == "vip") json["req_0"]["data"]["infoMap"]["123456"] = {
                {"is_vip", true}, {"vip_level", "5"}, {"vip_name", "Alias VIP"},
                {"vip_icon", "http://y.qq.com/explicit.png"}, {"hugeVip", true},
                {"svipIcon", "http://y.qq.com/super.png"}, {"vipEnd", "2000000000000"}
            };
            else json["req_1"]["data"]["map_userinfo"]["123456"] = {
                {"uin", "123456"}, {"nickname", "Alias QQ"}, {"logo", "http://q1.qlogo.cn/alias.jpg"}
            };
            response = JsonResponse(json);
        };
        auto stored = saved;
        const auto profile = Run(aliases, Operation::Refresh, stored);
        Check(profile.status == "signed-in" && profile.displayName == "Alias QQ" &&
              profile.avatarUrl == "https://q1.qlogo.cn/alias.jpg" && profile.memberships.size() == 2,
              "direct profile containers and field aliases are supported");
        // With SVIP active, the generic vip_name/vip_icon describe the top
        // tier: they never relabel or re-icon the green entry.
        if (profile.memberships.size() == 2)
            Check(profile.memberships[0].label == u8"\u7eff\u94bb" && profile.memberships[0].level == 5 &&
                  profile.memberships[0].active && profile.memberships[0].expiresAt == 2'000'000'000'000LL &&
                  profile.memberships[0].iconUrl == "https://y.qq.com/mediastyle/lv-icon/v14/2x/vip5.png" &&
                  profile.memberships[1].active && profile.memberships[1].iconUrl == "https://y.qq.com/super.png",
                  "boolean flags, numeric strings, millisecond expiry and explicit SVIP icon are preserved");
        Check(aliases.steps == std::vector<std::string>{"profile", "vip", "icons"},
              "the green icon is looked up rather than taken from the SVIP generic icon");
    }
    {
        // Live response shape (2026-10-11) of a 豪华绿钻 account without SVIP:
        // iVipFlag and iSuperVip set, HugeVip 0, the expiry as China time
        // text, and an icon list whose first entry is the GREEN vip5 artwork
        // with a desc mentioning svip, followed by an audiobook icon.
        // The recorded expiry (2026-10-27 20:21 +08:00) has since passed and
        // the membership state is derived from the wall clock, so the same
        // shape is pinned to a far-future date to keep this assertion stable.
        FakeQQ live;
        live.alter = [](const std::string& step, HttpResponse& response) {
            auto json = Json::parse(response.body);
            if (step == "vip") {
                json["req_0"]["data"]["infoMap"]["123456"] = {
                    {"iVipFlag", 1}, {"iSuperVip", 1}, {"HugeVip", 0}, {"ieight", 1}, {"iCurLevel", 5},
                    {"sOverDateTime", "2099-10-27 20:21:17"}, {"HugeVipEnd", ""}, {"sIcon", "opaque"}};
                response = JsonResponse(json);
            } else if (step == "icons") {
                response = MusicData({{"UserInfoUI", {{"iconlist", Json::array({
                    {{"desc", R"({"string10":"svip"})"}, {"srcUrl", "https://y.qq.com/mediastyle/lv-icon/v14/2x/vip5.png"}},
                    {{"desc", R"({"string10":"vip"})"}, {"srcUrl", "https://y.qq.com/mediastyle/lv-icon/v10/audio/2x/d-vip1.png"}}})}}}});
            }
        };
        auto liveStored = saved;
        const auto liveProfile = Run(live, Operation::Refresh, liveStored);
        Check(liveProfile.status == "signed-in" && liveProfile.memberships.size() == 2 &&
              liveProfile.memberships[0].active && liveProfile.memberships[0].label == u8"\u8c6a\u534e\u7eff\u94bb" &&
              liveProfile.memberships[0].expiresAt == 4'096'786'877'000LL &&
              liveProfile.memberships[0].iconUrl == "https://y.qq.com/mediastyle/lv-icon/v14/2x/vip5.png" &&
              !liveProfile.memberships[1].active &&
              liveProfile.memberships[1].iconUrl == "https://y.qq.com/mediastyle/lv-icon/v14/2x/d-svip5.png",
              "live shape: iSuperVip is 豪华绿钻 with its text expiry, SVIP stays inactive and dimmed");
    }
    {
        // Reported case: an SVIP-only account whose generic fields carry the
        // SVIP name and artwork, and an icon list in Chinese with SVIP first.
        FakeQQ svip;
        svip.alter = [](const std::string& step, HttpResponse& response) {
            auto json = Json::parse(response.body);
            if (step == "vip") {
                json["req_0"]["data"]["infoMap"]["123456"]["vip"] = {
                    {"isVip", 0}, {"HugeVip", 1}, {"vipLevel", 5}, {"vipName", u8"\u8d85\u7ea7\u4f1a\u5458"},
                    {"vipIcon", "http://y.qq.com/mediastyle/lv-icon/v14/2x/svip5.png"}, {"svipEnd", 2000000000}};
                response = JsonResponse(json);
            } else if (step == "icons") {
                response = MusicData({{"UserInfoUI", {{"iconlist", Json::array({
                    {{"desc", u8"\u8d85\u7ea7\u4f1a\u5458"}, {"srcUrl", "http://y.qq.com/mediastyle/lv-icon/v14/2x/svip5.png"}},
                    {{"desc", u8"\u7eff\u94bb"}, {"srcUrl", "http://y.qq.com/mediastyle/lv-icon/v14/2x/vip5.png"}}})}}}});
            }
        };
        auto stored = saved;
        const auto profile = Run(svip, Operation::Refresh, stored);
        Check(profile.status == "signed-in" && profile.memberships.size() == 2, "SVIP-only profile parses");
        if (profile.memberships.size() == 2) {
            const auto& green = profile.memberships[0];
            const auto& super = profile.memberships[1];
            Check(super.active && super.label == u8"\u8d85\u7ea7\u4f1a\u5458" &&
                  super.iconUrl == "https://y.qq.com/mediastyle/lv-icon/v14/2x/svip5.png",
                  "SVIP-only account: the SVIP entry is active with SVIP artwork");
            Check(!green.active && green.label == u8"\u7eff\u94bb" &&
                  green.iconUrl == "https://y.qq.com/mediastyle/lv-icon/v14/2x/d-vip5.png",
                  "SVIP-only account: green is inactive, labelled green, with dimmed green artwork");
        }
    }
    {
        FakeQQ partial;
        partial.alter = [](const std::string& step, HttpResponse& response) {
            if (step != "vip") return;
            auto json = Json::parse(response.body);
            json["req_0"]["data"]["infoMap"]["123456"]["vip"]["vipIcon"] = "https://y.qq.com/explicit.png";
            response = JsonResponse(json);
        };
        auto stored = saved;
        const auto profile = Run(partial, Operation::Refresh, stored);
        Check(profile.status == "signed-in" && profile.memberships.size() == 2 &&
              profile.memberships[0].iconUrl == "https://y.qq.com/explicit.png" &&
              profile.memberships[1].iconUrl == "https://y.qq.com/mediastyle/lv-icon/v14/2x/d-svip5.png" &&
              partial.steps == std::vector<std::string>{"profile", "vip", "icons"},
              "decoration query fills only missing icons without replacing explicit profile icons");
    }
    {
        FakeQQ zeroLevel;
        zeroLevel.alter = [](const std::string& step, HttpResponse& response) {
            if (step != "vip") return;
            auto json = Json::parse(response.body);
            json["req_0"]["data"]["infoMap"]["123456"]["vip"]["vipLevel"] = 0;
            response = JsonResponse(json);
        };
        auto stored = saved;
        const auto profile = Run(zeroLevel, Operation::Refresh, stored);
        Check(profile.status == "signed-in" && profile.memberships.size() == 2 &&
              profile.memberships[0].level == 0 && zeroLevel.steps == std::vector<std::string>{"profile", "vip"},
              "zero membership level uses fallback without a decoration request");
    }
    for (const auto operation : {Operation::Poll, Operation::Refresh}) {
        for (const bool active : {false, true}) {
            for (int variant = 0; variant < 11; ++variant) {
                FakeQQ decoration;
                decoration.alter = [variant, active](const std::string& step, HttpResponse& response) {
                    if (step == "vip") {
                        auto json = Json::parse(response.body);
                        auto& vip = json["req_0"]["data"]["infoMap"]["123456"]["vip"];
                        vip["isVip"] = active;
                        vip["HugeVip"] = active;
                        response = JsonResponse(json);
                    }
                    if (step != "icons") return;
                    if (variant == 0) throw std::runtime_error("music-key private decoration diagnostic");
                    if (variant == 1) response.status = 503;
                    if (variant == 2) response.body = "{malformed";
                    if (variant == 3) response = MusicData(Json::object());
                    if (variant == 4) response = JsonResponse({{"req_0", {{"code", 104009}}}});
                    if (variant == 5) response = MusicData({{"UserInfoUI", {{"iconlist", Json::object()}}}});
                    if (variant == 6) response = MusicData({{"UserInfoUI", {{"iconlist", Json::array({1})}}}});
                    if (variant == 7) response = MusicData({{"UserInfoUI", {{"iconlist",
                        std::vector<Json>(17, Json::object())}}}});
                    if (variant == 8) response = MusicData({{"UserInfoUI", {{"iconlist", Json::array({
                        {{"desc", "vip"}, {"srcUrl", "https://unofficial.invalid/vip.png"}},
                        {{"desc", "svip"}, {"srcUrl", "https://y.qq.com.evil.invalid/svip.png"}}
                    })}}}});
                    if (variant == 9) response = JsonResponse(Json::object());
                    if (variant == 10) response = JsonResponse({{"req_0", {{"code", 500}}}});
                };
                auto stored = operation == Operation::Poll ? SeedAttempt() : saved;
                const auto profile = Run(decoration, operation, stored);
                Check(profile.status == "signed-in" && profile.error.empty() &&
                      profile.avatarUrl == "https://q1.qlogo.cn/bju.jpg" && profile.memberships.size() == 2,
                      "failed or malformed decoration never rejects QQ login or refresh");
                if (profile.memberships.size() == 2)
                    Check(profile.memberships[0].active == active && profile.memberships[1].active == active &&
                          profile.memberships[0].iconUrl == std::string("https://y.qq.com/mediastyle/lv-icon/v14/2x/") +
                              (active ? "vip5.png" : "d-vip5.png") &&
                          profile.memberships[1].iconUrl == std::string("https://y.qq.com/mediastyle/lv-icon/v14/2x/") +
                              (active ? "svip5.png" : "d-svip5.png"),
                          "optional decoration failure selects official active or gray membership fallbacks");
                Check(stored.contains("credential") && !stored.contains("attempt") &&
                      stored.at("profile").at("memberships") == Json(profile.memberships),
                      "decoration failure still commits only complete credentials and public profile");
            }
        }
    }
    for (int variant = 0; variant < 5; ++variant) {
        FakeQQ profile;
        profile.alter = [variant](const std::string& step, HttpResponse& response) {
            if (step != (variant == 3 ? "profile" : "vip")) return;
            auto json = Json::parse(response.body);
            if (variant == 3) json["req_1"]["data"]["map_userinfo"]["123456"]["base"]["headurl"] =
                "https://user:private@avatar.test/image";
            else {
                auto& vip = json["req_0"]["data"]["infoMap"]["123456"]["vip"];
                if (variant == 0) vip["vipEnd"] = 1;
                if (variant == 1) vip["vipIcon"] = "javascript:private";
                if (variant == 2) vip["vipName"] = "private\nlabel";
                if (variant == 4) vip["vipLevel"] = 1001;
            }
            response = JsonResponse(json);
        };
        auto stored = saved;
        const auto result = Run(profile, Operation::Refresh, stored);
        if (variant == 0)
            Check(result.status == "signed-in" && result.memberships.size() == 2 && !result.memberships[0].active &&
                  result.memberships[0].expiresAt == 1000, "expired VIP is inactive without expiring the music login");
        else if (variant == 1)
            Check(result.status == "signed-in" && result.memberships.size() == 2 &&
                  result.memberships[0].iconUrl == "https://y.qq.com/mediastyle/lv-icon/v14/2x/vip5.png",
                  "unsafe optional VIP image is replaced by an official image");
        else if (variant == 3)
            Check(result.status == "signed-in" && result.avatarUrl == "https://q1.qlogo.cn/g?b=qq&nk=123456&s=100",
                  "unsafe optional avatar is replaced by the authenticated official QQ avatar");
        else
            Check(result.status == "signed-in" && result.error.empty() && result.avatarUrl == "https://q1.qlogo.cn/bju.jpg" &&
                  result.memberships.empty(), "unsafe VIP metadata is omitted without rejecting valid base identity");
    }
    for (const auto operation : {Operation::Poll, Operation::Refresh}) {
        for (int variant = 0; variant < 24; ++variant) {
            FakeQQ vipFailure;
            vipFailure.alter = [variant](const std::string& step, HttpResponse& response) {
                if (step != "vip") return;
                if (variant == 20) throw std::runtime_error("music-key private VIP transport diagnostic");
                if (variant == 21) { response.status = 503; return; }
                if (variant == 22) { response.body = "{malformed"; return; }
                auto json = Json::parse(response.body);
                if (variant == 0) json.erase("req_0");
                else if (variant == 1) json["req_0"] = nullptr;
                else if (variant == 2) json["req_0"]["code"] = 500;
                else if (variant == 3) json["req_0"]["code"] = 104009;
                else if (variant == 4) json["req_0"]["data"] = nullptr;
                else if (variant == 5) json["req_0"]["data"]["infoMap"] = Json::array();
                else if (variant == 6) json["req_0"]["data"]["infoMap"]["123456"] = "private";
                else if (variant == 7) json["req_0"]["data"]["infoMap"]["123456"]["vip"] = nullptr;
                else {
                    auto& vip = json["req_0"]["data"]["infoMap"]["123456"]["vip"];
                    if (variant == 8) vip["vipLevel"] = Json::object();
                    if (variant == 9) vip["vipLevel"] = -1;
                    if (variant == 10) vip["vipLevel"] = 1001;
                    if (variant == 11) vip["vipName"] = "private\nlabel";
                    if (variant == 12) vip["vipName"] = std::string(129, 'x');
                    if (variant == 13) vip["vipEnd"] = Json::array();
                    if (variant == 14) vip["isVip"] = Json::array();
                    if (variant == 15) vip["isSuperVip"] = Json::object();
                    if (variant == 16) vip["svipEnd"] = -1;
                    if (variant == 17) vip["vipLevel"] = 5.5;
                    if (variant == 18) json["req_0"]["code"] = "network";
                    if (variant == 19) json["req_0"]["data"]["code"] = 104009;
                    if (variant == 23) json["code"] = 104009;
                }
                response = JsonResponse(json);
            };
            auto stored = operation == Operation::Poll ? SeedAttempt() : saved;
            if (operation == Operation::Refresh) stored["credential"]["expiresAt"] = 1'000'000'001;
            const auto result = Run(vipFailure, operation, stored);
            Check(result.status == "signed-in" && result.error.empty() && result.userId == "123456" &&
                  result.displayName == "Test QQ" && result.avatarUrl == "https://q1.qlogo.cn/bju.jpg" &&
                  result.memberships.empty(), "valid base identity signs in despite malformed VIP data or failed VIP requests");
            Check(stored.at("credential").at("musicKey") ==
                      (operation == Operation::Poll ? "music-key" : "renewed-music-key") &&
                  !stored.contains("attempt") && stored.at("profile").at("memberships").empty() &&
                  std::find(vipFailure.steps.begin(), vipFailure.steps.end(), "icons") == vipFailure.steps.end(),
                  "optional VIP failure cannot discard fresh credentials or trigger icon requests");
        }
    }
    for (const auto operation : {Operation::Poll, Operation::Refresh}) {
        for (const auto* stage : {"vip", "icons"}) {
            FakeQQ cancelledVip;
            bool cancel = false;
            cancelledVip.alter = [stage](const std::string& step, HttpResponse& response) {
                if (step != stage) return;
                auto json = Json::parse(response.body);
                json["req_0"]["code"] = 104009;
                response = JsonResponse(json);
            };
            cancelledVip.after = [&](const std::string& step) { if (step == stage) cancel = true; };
            auto stored = operation == Operation::Poll ? SeedAttempt() : saved;
            const auto before = stored;
            bool threw = false;
            try { Run(cancelledVip, operation, stored, [&] { return cancel; }); }
            catch (const std::runtime_error& e) { threw = std::string(e.what()) == "cancelled"; }
            Check(threw && stored == before, "optional VIP/icon errors cannot swallow cancellation or commit stale credentials");
        }
    }

    {
        FakeQQ cleared;
        cleared.alter = [](const std::string& step, HttpResponse& response) {
            if (step == "check") {
                response.cookies.push_back("pt_login_sig=; Path=/; Max-Age=0; Secure");
                response.cookies.push_back("pt_oauth_token=; Expires=Thu, 01 Jan 1970 00:00:00 GMT; Path=/");
            }
        };
        auto attempt = SeedAttempt();
        Check(Run(cleared, Operation::Poll, attempt).status == "signed-in",
              "successful check_sig may clear unrelated cookies without rejecting authorization");
    }
    {
        FakeQQ duplicateSessionCookie;
        duplicateSessionCookie.alter = [](const std::string& step, HttpResponse& response) {
            if (step == "check") response.cookies.push_back("p_skey=; Path=/; Max-Age=0; Secure");
        };
        auto attempt = SeedAttempt();
        Check(Run(duplicateSessionCookie, Operation::Poll, attempt).status == "signed-in",
              "expired duplicate p_skey must not overwrite the valid QQ session cookie");
    }
    {
        FakeQQ escaped;
        escaped.callback =
            "https:\\/\\/ssl.ptlogin2.graph.qq.com\\/check_sig?uin=123456\\x26ptsigx=sig%2Bx%3D\\u0026s_url=x";
        auto attempt = SeedAttempt();
        Check(Run(escaped, Operation::Poll, attempt).status == "signed-in",
              "JavaScript-escaped callback separators decoded before strict endpoint validation");
    }
    {
        FakeQQ malformed;
        malformed.alter = [](const std::string& step, HttpResponse& response) {
            if (step == "poll") response.body = "ptuiCB('0','0','unterminated);";
        };
        auto attempt = SeedAttempt();
        Check(Run(malformed, Operation::Poll, attempt).error == "qq-status-response",
              "unterminated callback fails without an infinite parse loop");
    }

    fake.steps.clear();
    reply = Run(fake, Operation::Refresh, secrets);
    Check(reply.status == "signed-in" && reply.avatarUrl == "https://q1.qlogo.cn/bju.jpg" &&
          reply.memberships.size() == 2 && fake.steps == std::vector<std::string>{"profile", "vip", "icons"},
          "valid credential refresh reloads avatar and membership profile");
    secrets["credential"].erase("refreshToken");
    secrets["credential"].erase("refreshKey");
    Check(Run(fake, Operation::Refresh, secrets).status == "signed-in", "unexpired credentials do not require refresh material");
    secrets = saved;
    secrets["credential"]["expiresAt"] = 1'000'000'001;
    fake.steps.clear();
    Check(Run(fake, Operation::Refresh, secrets).status == "signed-in" &&
          fake.steps == std::vector<std::string>{"refresh", "profile", "vip", "icons"}, "expired key token refresh then profile/VIP/icons");
    Check(secrets["credential"]["musicKey"] == "renewed-music-key" &&
          secrets["credential"]["refreshToken"] == "refresh-token" &&
          secrets["credential"]["openId"] == "open-id", "refresh retains omitted optional material");

    for (const auto* expiry : {"absolute-ms", "lifetime-created", "lifetime-updated"}) {
        secrets = saved;
        if (std::string(expiry) == "absolute-ms") secrets["credential"]["expiresAt"] = 1'000'000'000'001LL;
        else {
            secrets["credential"]["keyExpiresIn"] = 1;
            secrets["credential"][std::string(expiry) == "lifetime-created" ? "musickeyCreateTime" : "updatedAt"] = 1;
        }
        secrets["credential"].erase("refreshToken");
        const auto before = fake.requests.size();
        Check(Run(fake, Operation::Refresh, secrets).status == "expired" &&
              !secrets.contains("credential") && !secrets.contains("profile") &&
              fake.requests.size() == before, "expired credential without refresh is cleared without HTTP");
    }

    for (int code : {1000, 104009, 104013, 104400, 104401}) {
        secrets = saved;
        secrets["credential"]["expiresAt"] = 1'000'000'001;
        FakeQQ rejected;
        rejected.alter = [code](const std::string& step, HttpResponse& response) {
            if (step == "refresh") response = JsonResponse({{"req_0", {{"code", code}}}});
        };
        Check(Run(rejected, Operation::Refresh, secrets).status == "expired" &&
              !secrets.contains("credential") && !secrets["device"].contains("sessionSid"),
              "explicit credential rejection clears native authorization");
    }
    for (const auto* step : {"refresh", "profile"}) {
        secrets = saved;
        secrets["credential"]["expiresAt"] = 1'000'000'001;
        const auto before = secrets;
        FakeQQ failed;
        failed.alter = [step](const std::string& current, HttpResponse&) {
            if (current == step) throw std::runtime_error("music-key refresh-token private transport diagnostic");
        };
        const auto error = Run(failed, Operation::Refresh, secrets);
        if (std::string(step) == "refresh")
            Check(error.status == "error" && error.error == "network" && SameExceptDevice(secrets, before),
                  "temporary credential refresh failure preserves original credential");
        else
            Check(error.status == "signed-in" && error.userId == "123456" &&
                secrets["credential"]["expiresAt"] != before["credential"]["expiresAt"],
                "verified refreshed credential survives a temporary profile failure");
    }
    for (const auto* badCallback : {
        "https://evil.invalid/check_sig?uin=123456&ptsigx=x",
        "https://ssl.ptlogin2.graph.qq.com.evil.invalid/check_sig?uin=123456&ptsigx=x",
        "https://ssl.ptlogin2.graph.qq.com/check_sig?notuin=123456&ptsigx=x",
        "https://ssl.ptlogin2.graph.qq.com/check_sig?uin=123456&uin=654321&ptsigx=x",
        "https://ssl.ptlogin2.graph.qq.com/check_sig?uin=123456&ptsigx=%ZZ",
        "https://ssl.ptlogin2.graph.qq.com/check_sig?uin=123456&ptsigx=%0D%0AInjected"
    }) {
        secrets = SeedAttempt();
        FakeQQ bad;
        bad.callback = badCallback;
        Check(Run(bad, Operation::Poll, secrets).status == "error" &&
              bad.steps == std::vector<std::string>{"poll"}, "hostile/incomplete callback rejected before HTTP");
    }
    for (const auto* badLocation : {
        "https://evil.invalid/?code=x&state=state",
        "https://y.qq.com/portal/wx_redirect.html?notcode=x&state=state",
        "https://y.qq.com/portal/wx_redirect.html?code=x&code=y&state=state",
        "https://y.qq.com/portal/wx_redirect.html?code=x&state=wrong"
    }) {
        secrets = SeedAttempt();
        FakeQQ bad;
        bad.location = badLocation;
        Check(Run(bad, Operation::Poll, secrets).status == "error" &&
              bad.steps == std::vector<std::string>{"poll", "check", "authorize"},
              "untrusted OAuth location is never navigated");
    }
    for (const auto* stage : {"qimei", "session", "login", "profile"}) {
        secrets = SeedAttempt();
        const auto before = secrets;
        FakeQQ malformed;
        malformed.alter = [stage](const std::string& step, HttpResponse& response) {
            if (step == stage) response = JsonResponse(Json::object());
        };
        const auto malformedReply = Run(malformed, Operation::Poll, secrets);
        Check(malformedReply.status == "error" && SameExceptDevice(secrets, before) &&
              malformedReply.error == (std::string(stage) == "profile" ? "qq-profile-response" :
                  std::string(stage) == "login" ? "qq-credential-malformed" : "qq-device-response"),
              "malformed exchange cannot commit partial credentials");
    }
    for (int variant = 0; variant < 5; ++variant) {
        secrets = saved;
        secrets["attempt"] = SeedAttempt()["attempt"];
        const auto before = secrets;
        FakeQQ invalid;
        invalid.alter = [variant](const std::string& step, HttpResponse& response) {
            if (step == "login" && variant == 0) {
                auto json = Json::parse(response.body);
                json["code"] = 104009;
                response = JsonResponse(json);
            }
            if (step == "login" && variant == 1)
                response = MusicData({
                    {"code", 104009}, {"musicid", 123456}, {"musickey", "must-not-be-accepted"}});
            if (step == "profile" && variant >= 2) {
                auto json = Json::parse(response.body);
                if (variant == 2) json["req_1"]["data"]["map_userinfo"] = Json::object();
                if (variant == 3) json["req_1"]["data"]["map_userinfo"]["123456"]["base"]["uin"] = 654321;
                if (variant == 4) json["req_1"]["code"] = 104009;
                response = JsonResponse(json);
            }
        };
        Check(Run(invalid, Operation::Poll, secrets).status == "error" && SameExceptDevice(secrets, before),
              "valid music key alone or mismatched/rejected profile is never signed-in");
    }
    for (int variant = 0; variant < 5; ++variant) {
        secrets = saved;
        const auto before = secrets;
        FakeQQ badQr;
        badQr.alter = [variant](const std::string&, HttpResponse& response) {
            if (variant == 0) response.cookies.clear();
            if (variant == 1) response.body = "<html>not a PNG</html>";
            if (variant == 2) response.cookies = {"qrsig=fake\r\nInjected: bad"};
            if (variant == 3) response.body.resize(512 * 1024, 'x');
            if (variant == 4) response.status = 503;
        };
        Check(Run(badQr, Operation::Begin, secrets).status == "error" && SameExceptDevice(secrets, before),
              "invalid QR response preserves existing native state");
    }
    for (std::size_t stop = 0; stop <= 9; ++stop) {
        secrets = SeedAttempt();
        const auto before = secrets;
        FakeQQ cancelled;
        bool cancel = stop == 0;
        cancelled.after = [&](const std::string&) { cancel = cancelled.steps.size() >= stop; };
        bool threw = false;
        try { Run(cancelled, Operation::Poll, secrets, [&] { return cancel; }); }
        catch (const std::runtime_error& e) { threw = std::string(e.what()) == "cancelled"; }
        Check(threw && secrets == before && cancelled.steps.size() == stop,
              "cancellation at every exchange boundary stops traffic and commits nothing");
    }
    secrets = saved;
    const auto device = secrets["device"];
    Run(fake, Operation::Begin, secrets);
    Check(secrets["device"] == device, "begin preserves stable native Android identity");
    fake.pollCode = 65;
    Check(Run(fake, Operation::Poll, secrets).status == "expired" && !secrets.contains("attempt"),
          "expired QR removes only temporary challenge");
    secrets = Json::object();
    FakeQQ empty;
    Check(Run(empty, Operation::Refresh, secrets).status == "signed-out" && empty.steps.empty(),
          "no stored credentials means signed-out without HTTP");
    Check(Run(empty, Operation::Poll, secrets).status == "expired" && empty.steps.empty(),
          "missing challenge expires without HTTP");
    if (!passed) return 1;
    std::cout << "QQ account adapter passed (fake HTTP only)\n";
    return 0;
}
