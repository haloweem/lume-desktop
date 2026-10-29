#include "apps/shell/music_provider_account_session.h"
#include <mutex>
#include <vector>
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace lume::music::auth;
using namespace lume::shell;
using namespace std::chrono_literals;
namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
Json State(MusicProviderAccountSession& session) {
    const auto text = session.State();
    auto state = Json::parse(text.begin(), text.end());
    Require(state.is_object() && state.size() == 4 && state.at("version") == 1 &&
        state.at("type") == "music-provider-account-state" && state.at("revision").is_number_unsigned() &&
        state.at("accounts").is_array() && state.at("accounts").size() == 3, "exact account state envelope");
    for (const auto& account : state.at("accounts")) {
        Require(account.is_object() && account.size() == 11 && account.at("request").is_number_unsigned() &&
            account.at("memberships").is_array(), "exact account schema");
        for (const auto* key : {"provider", "status", "phase", "userId", "displayName",
                               "qrImage", "qrText", "error", "avatarUrl"})
            Require(account.at(key).is_string(), "account public string fields");
        for (const auto& membership : account.at("memberships")) {
            Require(membership.is_object() && membership.size() == 6 &&
                membership.at("kind").is_string() && membership.at("label").is_string() &&
                membership.at("level").is_number_integer() && membership.at("active").is_boolean() &&
                membership.at("iconUrl").is_string() && membership.at("expiresAt").is_number_integer(),
                "exact six-field membership schema");
        }
    }
    return state;
}
bool Await(const std::function<bool()>& predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(2ms);
    }
    return true;
}
Reply SignedIn() {
    Reply reply;
    reply.status = "signed-in";
    reply.userId = "123";
    reply.displayName = "Fixture";
    reply.avatarUrl = "https://q1.qlogo.cn/fixture.jpg";
    reply.memberships = {{"qq-green-vip", "Green VIP", 5, true, "https://y.qq.com/vip5.png", 2'000'000'000'000LL}};
    return reply;
}
void Identity(const Json& account, bool present) {
    Require(account.at("avatarUrl") == (present ? "https://q1.qlogo.cn/fixture.jpg" : "") &&
        account.at("memberships") == (present ? Json::array({{
            {"kind", "qq-green-vip"}, {"label", "Green VIP"}, {"level", 5}, {"active", true},
            {"iconUrl", "https://y.qq.com/vip5.png"}, {"expiresAt", 2'000'000'000'000LL}
        }}) : Json::array()), "avatar and memberships match committed identity");
}
Reply Authorizing() {
    Reply reply;
    reply.status = "authorizing";
    reply.phase = "waiting";
    reply.qrText = "https://example.invalid/fixture";
    return reply;
}
Reply ErrorReplyFixture(const char* error) {
    Reply reply;
    reply.status = "error";
    reply.error = error;
    return reply;
}
MusicProviderAccountOptions Fixtures() {
    MusicProviderAccountOptions options;
    options.verification = true;
    options.pollInterval = 5ms;
    options.load = [](Provider) { return Json::object(); };
    options.save = [](Provider, const Json&) {};
    options.clear = [](Provider) {};
    return options;
}
void CredentialErrorContract() {
    for (const auto& error : {"qq-credential-code-104009", "qq-credential-http-503",
                              "qq-credential-network", "qq-credential-malformed",
                              "qq-credential-code-1234567890", "qq-credential-code-104009-secret"}) {
        auto options = Fixtures();
        options.execute = [error](Provider, Operation, Json&, const Cancelled&) {
            Reply reply;
            reply.status = "error";
            reply.error = error;
            return reply;
        };
        MusicProviderAccountSession session(options);
        session.HandlePage({"begin", Provider::QQ, 1});
        Require(Await([&] { return State(session)["accounts"][1]["status"] == "error"; }),
            "QQ diagnostic completed");
        const auto publicError = State(session)["accounts"][1]["error"].get<std::string>();
        const bool allowed = error == std::string("qq-credential-code-104009") ||
            error == std::string("qq-credential-http-503") ||
            error == std::string("qq-credential-network") ||
            error == std::string("qq-credential-malformed");
        Require(publicError == (allowed ? error : "invalid-response"), "QQ diagnostic public boundary");
        session.Shutdown();
    }
}
void ShutdownLogout() {
    std::atomic<bool> entered{false}, cancelled{false};
    std::atomic<int> writes{0}, clears{0};
    auto options = Fixtures();
    options.execute = [&](Provider, Operation, Json& secrets, const Cancelled& stop) {
        entered = true;
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        while (!stop() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(1ms);
        cancelled = stop();
        secrets["secret"] = "late-native-secret";
        return SignedIn();
    };
    options.save = [&](Provider, const Json&) { ++writes; };
    options.clear = [&](Provider provider) {
        Require(provider != Provider::Netease, "logout must stay provider scoped");
        ++clears;
    };
    MusicProviderAccountSession session(options);
    session.HandlePage({"begin", Provider::Netease, 1});
    Require(Await([&] { return entered.load(); }), "blocked provider started");
    session.HandlePage({"logout", Provider::QQ, 2});
    session.HandlePage({"logout", Provider::Kugou, 3});
    const auto queued = State(session);
    session.HandlePage({"cancel", Provider::QQ, 4});
    session.HandlePage({"refresh", Provider::QQ, 5});
    session.HandlePage({"begin", Provider::QQ, 6});
    const bool preserved = State(session) == queued;
    session.Shutdown();
    const auto state = State(session);
    Require(cancelled && writes == 0, "shutdown must reject the late sign-in");
    Require(preserved && clears == 2 && state["accounts"][1]["status"] == "signed-out" &&
        state["accounts"][2]["status"] == "signed-out", "shutdown must drain queued durable logouts");
    session.Shutdown();
    Require(clears == 2, "shutdown logout must run exactly once");
}
void RestoreRetry(bool explicitCancel) {
    std::atomic<int> loads{0}, writes{0};
    std::atomic<bool> entered{false};
    auto options = Fixtures();
    options.load = [&](Provider provider) {
        if (provider != Provider::Netease) return Json::object();
        ++loads;
        return Json{{"secret", "restore-native-secret"}};
    };
    options.execute = [&](Provider, Operation operation, Json&, const Cancelled& stop) {
        Require(operation == Operation::Refresh, "restore uses refresh");
        if (loads == 1) {
            entered = true;
            const auto deadline = std::chrono::steady_clock::now() + 3s;
            while (!stop() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(1ms);
        }
        return SignedIn();
    };
    options.save = [&](Provider, const Json&) { ++writes; };
    MusicProviderAccountSession session(options);
    session.HandlePage({"snapshot"});
    Require(Await([&] { return entered.load(); }), "initial restore entered");
    if (explicitCancel) session.HandlePage({"cancel", Provider::Netease, 1});
    else session.CancelActive();
    session.HandlePage({"snapshot"});
    const bool restored = Await([&] { return State(session)["accounts"][0]["status"] == "signed-in"; });
    session.Shutdown();
    Require(restored && loads == 2 && writes == 1, "cancelled restore must retry on next snapshot");
    Identity(State(session)["accounts"][0], true);
    Require(State(session).dump().find("restore-native-secret") == std::string::npos, "restore secret leaked");
}
void StaleCommands() {
    std::atomic<bool> entered{false}, release{false}, cancelled{false};
    std::atomic<int> calls{0}, writes{0}, clears{0};
    auto options = Fixtures();
    options.execute = [&](Provider, Operation, Json& secrets, const Cancelled& stop) {
        ++calls;
        entered = true;
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        while (!release && !stop() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(1ms);
        cancelled = stop();
        secrets["secret"] = "fenced-native-secret";
        return SignedIn();
    };
    options.save = [&](Provider, const Json&) { ++writes; };
    options.clear = [&](Provider) { ++clears; };
    MusicProviderAccountSession session(options);
    session.HandlePage({"begin", Provider::QQ, 10});
    Require(Await([&] { return entered.load(); }), "current request entered");
    const auto before = State(session);
    for (const auto* action : {"begin", "cancel", "refresh", "logout"}) {
        session.HandlePage({action, Provider::QQ, 9});
        session.HandlePage({action, Provider::QQ, 10});
    }
    const bool unchanged = State(session) == before;
    release = true;
    const bool signedIn = Await([&] { return State(session)["accounts"][1]["status"] == "signed-in"; });
    session.Shutdown();
    Require(unchanged && signedIn && !cancelled && calls == 1 && writes == 1 && clears == 0,
        "stale and duplicate commands must not change state, cancel or perform storage");
}
void ExpiryBeforeNetwork() {
    std::atomic<int> calls{0}, writes{0};
    auto options = Fixtures();
    options.qrLifetime = 0ms;
    options.execute = [&](Provider, Operation, Json&, const Cancelled&) { ++calls; return SignedIn(); };
    options.save = [&](Provider, const Json&) { ++writes; };
    MusicProviderAccountSession session(options);
    session.HandlePage({"begin", Provider::QQ, 1});
    const bool expired = Await([&] { return State(session)["accounts"][1]["status"] == "expired"; });
    session.Shutdown();
    Require(expired && calls == 0 && writes == 0, "already expired begin must not call network or save");
}
void ExpiryAfterLoad() {
    std::atomic<int> calls{0}, writes{0};
    auto options = Fixtures();
    options.qrLifetime = 15ms;
    options.load = [](Provider) { std::this_thread::sleep_for(40ms); return Json::object(); };
    options.execute = [&](Provider, Operation, Json&, const Cancelled&) { ++calls; return SignedIn(); };
    options.save = [&](Provider, const Json&) { ++writes; };
    MusicProviderAccountSession session(options);
    session.HandlePage({"begin", Provider::QQ, 1});
    const bool expired = Await([&] { return State(session)["accounts"][1]["status"] == "expired"; });
    session.Shutdown();
    Require(expired && calls == 0 && writes == 0, "load cannot consume QR deadline and still start network");
}
void ExpiryDuringRequest(bool poll, bool throws = false) {
    std::atomic<int> writes{0};
    std::atomic<bool> observedExpiry{false};
    auto options = Fixtures();
    options.qrLifetime = 100ms;
    options.execute = [&](Provider, Operation operation, Json& secrets, const Cancelled& stop) {
        if (poll && operation == Operation::Begin) return Authorizing();
        const auto deadline = std::chrono::steady_clock::now() + 300ms;
        while (!stop() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(1ms);
        observedExpiry = stop();
        if (throws) CheckCancelled(stop);
        secrets["secret"] = "expired-native-secret";
        // Even a transport ignoring cancellation must not commit a late success.
        return SignedIn();
    };
    options.save = [&](Provider, const Json&) { ++writes; };
    MusicProviderAccountSession session(options);
    session.HandlePage({"begin", Provider::QQ, 1});
    const bool expired = Await([&] { return State(session)["accounts"][1]["status"] == "expired"; });
    const auto state = State(session);
    session.Shutdown();
    Require(expired && observedExpiry && writes == 0, "QR deadline must cancel exchange and reject late success");
    Require(state["accounts"][1]["error"] == "expired" && state["accounts"][1]["qrText"] == "" &&
        state["accounts"][1]["userId"] == "" && state.dump().find("expired-native-secret") == std::string::npos,
        "expired attempt must expose neither QR nor identity nor credentials");
    Identity(state["accounts"][1], false);
}
void ExpiryDuringPollWait() {
    std::atomic<int> calls{0};
    auto options = Fixtures();
    options.qrLifetime = 40ms;
    options.pollInterval = 2s;
    options.execute = [&](Provider, Operation, Json&, const Cancelled&) { ++calls; return Authorizing(); };
    MusicProviderAccountSession session(options);
    session.HandlePage({"begin", Provider::QQ, 1});
    const auto deadline = std::chrono::steady_clock::now() + 500ms;
    while (State(session)["accounts"][1]["status"] != "expired" && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(2ms);
    const bool expired = State(session)["accounts"][1]["status"] == "expired";
    session.Shutdown();
    Require(expired && calls == 1, "expiry must wake before next poll without another network request");
}
void LogoutRetry() {
    std::atomic<int> clears{0};
    auto options = Fixtures();
    options.load = [](Provider) { return Json{{"secret", "retained-native-secret"}}; };
    options.execute = [](Provider, Operation, Json&, const Cancelled&) { return SignedIn(); };
    options.clear = [&](Provider) {
        if (++clears == 1) throw std::runtime_error("fixture-private-storage-detail");
    };
    MusicProviderAccountSession session(options);
    session.HandlePage({"refresh", Provider::QQ, 1});
    Require(Await([&] { return State(session)["accounts"][1]["status"] == "signed-in"; }), "retry identity restored");
    session.HandlePage({"logout", Provider::QQ, 2});
    Require(Await([&] { return State(session)["accounts"][1]["status"] == "error"; }), "logout storage failure");
    const auto failed = State(session);
    Identity(failed["accounts"][1], true);
    const bool identity = failed["accounts"][1]["userId"] == "123" &&
        failed["accounts"][1]["displayName"] == "Fixture" && failed["accounts"][1]["error"] == "storage";
    session.CancelActive();
    const bool failurePreserved = State(session) == failed;
    session.HandlePage({"logout", Provider::QQ, 3});
    const bool loggedOut = Await([&] { return State(session)["accounts"][1]["status"] == "signed-out"; });
    const auto after = State(session);
    Identity(after["accounts"][1], false);
    session.Shutdown();
    Require(identity && failurePreserved && loggedOut && clears == 2 && after["accounts"][1]["userId"] == "",
        "failed logout must keep retry identity until successful clear");
    Require(failed.dump().find("native-secret") == std::string::npos &&
        failed.dump().find("fixture-private-storage-detail") == std::string::npos, "logout error leaked private data");
}
void CancelRefresh() {
    std::atomic<int> calls{0}, writes{0};
    std::atomic<bool> entered{false};
    auto options = Fixtures();
    options.execute = [&](Provider, Operation, Json&, const Cancelled& stop) {
        if (++calls == 2) {
            entered = true;
            const auto deadline = std::chrono::steady_clock::now() + 3s;
            while (!stop() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(1ms);
            auto late = SignedIn();
            late.avatarUrl = "https://q1.qlogo.cn/late.jpg";
            late.memberships.clear();
            return late;
        }
        return SignedIn();
    };
    options.save = [&](Provider, const Json&) { ++writes; };
    MusicProviderAccountSession session(options);
    session.HandlePage({"refresh", Provider::QQ, 1});
    Require(Await([&] { return State(session)["accounts"][1]["status"] == "signed-in"; }), "initial profile restored");
    session.HandlePage({"refresh", Provider::QQ, 2});
    Require(Await([&] { return entered.load(); }), "replacement refresh entered");
    Identity(State(session)["accounts"][1], true);
    session.HandlePage({"cancel", Provider::QQ, 3});
    session.Shutdown();
    Identity(State(session)["accounts"][1], true);
    Require(writes == 1, "cancelled refresh cannot replace committed profile");
}
void InvalidPublicProfile() {
    for (int variant = 0; variant < 18; ++variant) {
        std::atomic<int> writes{0};
        auto options = Fixtures();
        options.execute = [variant](Provider, Operation, Json&, const Cancelled&) {
            auto reply = SignedIn();
            auto& membership = reply.memberships.front();
            if (variant == 0) reply.avatarUrl = "javascript:private-data";
            if (variant == 1) reply.avatarUrl = "https://user:private-data@avatar.test/image";
            if (variant == 2) membership.iconUrl = "file:///private-data";
            if (variant == 3) membership.label = "\xff";
            if (variant == 4) membership.label = "private\nlabel";
            if (variant == 5) membership.expiresAt = -1;
            if (variant == 6) membership.level = -1;
            if (variant == 7) membership.expiresAt = 9'007'199'254'740'992LL;
            if (variant == 8) reply.memberships.resize(9, membership);
            if (variant == 9) membership.kind.clear();
            if (variant == 10) membership.label.resize(129, 'x');
            if (variant == 11) reply.avatarUrl.resize(2049, 'x');
            if (variant == 12) membership.kind = "unknown-vip";
            if (variant == 13) membership.level = 1001;
            if (variant == 14) reply.avatarUrl = "https://avatar.test/image";
            if (variant == 15) membership.iconUrl = "https://y.qq.com.evil.invalid/image";
            if (variant == 16) reply.avatarUrl = "https://q1.qlogo.cn:8443/image";
            if (variant == 17) membership.iconUrl = "http://y.qq.com/image";
            return reply;
        };
        options.save = [&](Provider, const Json&) { ++writes; };
        MusicProviderAccountSession session(options);
        session.HandlePage({"refresh", Provider::QQ, 1});
        Require(Await([&] { return State(session)["accounts"][1]["status"] == "error"; }), "invalid public profile rejected");
        const auto account = State(session)["accounts"][1];
        session.Shutdown();
        Identity(account, false);
        Require(account["error"] == "invalid-response" && writes == 0, "invalid public metadata cannot persist or leak");
    }
}
void PublicImageOrigins() {
    Require(PublicImageUrl(""), "empty optional image accepted");
    for (const auto* host : {"y.qq.com", "y.gtimg.cn", "q1.qlogo.cn", "q2.qlogo.cn", "thirdqq.qlogo.cn",
                            "p1.music.126.net", "p2.music.126.net", "p3.music.126.net", "p4.music.126.net",
                            "imge.kugou.com", "imgessl.kugou.com"}) {
        Require(PublicImageUrl(std::string("https://") + host + "/fixture.png"), "official HTTPS image accepted");
        Require(PublicImageUrl(std::string("https://") + host + ":443/fixture.png"), "default TLS port accepted");
        Require(!PublicImageUrl(std::string("http://") + host + "/fixture.png"), "HTTP public image rejected");
        Require(!PublicImageUrl(std::string("https://") + host + ".evil.invalid/fixture.png"), "spoofed host rejected");
    }
    for (const auto* url : {"https://avatar.test/image", "https://user:private@y.qq.com/image",
                           "https://y.qq.com@evil.invalid/image", "https://y.qq.com:8443/image",
                           "https://y.qq.com:4430/image", "https://y.qq.com/\\evil.invalid",
                           "https://y.qq.com/image\nprivate", "https://y.qq.com/image private"})
        Require(!PublicImageUrl(url), "unsafe public image rejected");
}
void PublicProfileLimits() {
    auto options = Fixtures();
    options.execute = [](Provider, Operation, Json&, const Cancelled&) {
        auto reply = SignedIn();
        reply.memberships.front().level = 1000;
        reply.memberships.resize(8, reply.memberships.front());
        return reply;
    };
    MusicProviderAccountSession session(options);
    session.HandlePage({"refresh", Provider::QQ, 1});
    Require(Await([&] { return State(session)["accounts"][1]["status"] == "signed-in"; }), "maximum public profile accepted");
    const auto account = State(session)["accounts"][1];
    session.Shutdown();
    Require(account["memberships"].size() == 8 && account["memberships"][0]["level"] == 1000,
            "eight memberships and level 1000 cross the public bridge");
}
void RefreshProfile() {
    std::atomic<int> calls{0}, writes{0};
    auto options = Fixtures();
    options.execute = [&](Provider, Operation, Json&, const Cancelled&) {
        auto reply = SignedIn();
        const auto call = ++calls;
        if (call == 2) {
            reply.avatarUrl.clear();
            reply.memberships.clear();
        } else if (call == 3) {
            reply = {};
            reply.status = "error";
            reply.error = "network";
        } else if (call == 4) {
            reply.avatarUrl = "https://q1.qlogo.cn/uncommitted.jpg";
            reply.memberships.front().label = "Uncommitted";
        }
        return reply;
    };
    options.save = [&](Provider, const Json&) {
        if (++writes == 3) throw std::runtime_error("private-storage-detail");
    };
    MusicProviderAccountSession session(options);
    for (std::uint64_t request = 1; request <= 4; ++request) {
        session.HandlePage({"refresh", Provider::QQ, request});
        Require(Await([&] {
            const auto account = State(session)["accounts"][1];
            return account["request"] == request && account["status"] == (request >= 3 ? "error" : "signed-in");
        }), "refresh completes");
        Identity(State(session)["accounts"][1], true);
    }
    session.HandlePage({"logout", Provider::QQ, 5});
    Require(Await([&] { return State(session)["accounts"][1]["status"] == "signed-out"; }), "refresh identity logout");
    Identity(State(session)["accounts"][1], false);
    session.Shutdown();
    Require(writes == 3, "network/storage failures preserve only committed profile");
}
void PollRetriesTransientFailures() {
    std::atomic<int> polls{0}, writes{0};
    auto options = Fixtures();
    options.execute = [&](Provider, Operation operation, Json&, const Cancelled&) {
        if (operation == Operation::Begin) return Authorizing();
        Reply reply;
        const auto poll = ++polls;
        if (poll == 1 || poll == 2) {
            reply.status = "error";
            reply.error = poll == 1 ? "network" : "qq-status-response";
            return reply;
        }
        return SignedIn();
    };
    options.save = [&](Provider, const Json&) { ++writes; };
    MusicProviderAccountSession session(options);
    session.HandlePage({"begin", Provider::QQ, 1});
    // The QR must survive transient poll failures; the user should not have to
    // scan a new code after a single network hiccup.
    Require(Await([&] { return State(session)["accounts"][1]["status"] == "signed-in"; }),
        "transient poll failures keep the QR login alive");
    Require(polls == 3 && writes == 1, "retried polls still save exactly one identity");
    session.Shutdown();
}
void PollFailureCapStillEndsTheAttempt() {
    std::atomic<int> polls{0};
    auto options = Fixtures();
    options.execute = [&](Provider, Operation operation, Json&, const Cancelled&) {
        if (operation == Operation::Begin) return Authorizing();
        ++polls;
        Reply reply;
        reply.status = "error";
        reply.error = "network";
        return reply;
    };
    MusicProviderAccountSession session(options);
    session.HandlePage({"begin", Provider::QQ, 1});
    Require(Await([&] { return State(session)["accounts"][1]["status"] == "error"; }),
        "persistent failures eventually end the attempt");
    Require(polls == 4, "three retries after the first poll failure");
    session.Shutdown();
}
void BeginRetriesNetworkFailureOnce() {
    std::atomic<int> begins{0};
    auto options = Fixtures();
    options.beginRetryDelay = 5ms;
    options.execute = [&](Provider, Operation operation, Json&, const Cancelled&) {
        if (operation != Operation::Begin) return Authorizing();
        if (++begins == 1) return ErrorReplyFixture("network");
        return Authorizing();
    };
    MusicProviderAccountSession session(options);
    session.HandlePage({"begin", Provider::Kugou, 1});
    Require(Await([&] { return State(session)["accounts"][2]["status"] == "authorizing"; }),
        "a QR fetch that failed on the network is retried before it is shown");
    Require(begins == 2, "exactly one retry");
    session.Shutdown();
    // A refusal from the platform is not retried.
    std::atomic<int> refused{0};
    options.execute = [&](Provider, Operation, Json&, const Cancelled&) { ++refused; return ErrorReplyFixture("service"); };
    MusicProviderAccountSession other(options);
    other.HandlePage({"begin", Provider::Kugou, 1});
    Require(Await([&] { return State(other)["accounts"][2]["status"] == "error"; }), "service failure shown");
    Require(refused == 1, "service failure is not retried");
    other.Shutdown();
}
void ScannedButUnconfirmedExpires() {
    auto options = Fixtures();
    options.scannedLifetime = 30ms;
    options.execute = [&](Provider, Operation, Json&, const Cancelled&) {
        auto reply = Authorizing();
        reply.phase = "scanned";
        return reply;
    };
    MusicProviderAccountSession session(options);
    session.HandlePage({"begin", Provider::Netease, 1});
    Require(Await([&] { return State(session)["accounts"][0]["status"] == "expired"; }),
        "a code scanned but never confirmed expires before the QR lifetime");
    const auto account = State(session)["accounts"][0];
    Require(account["error"] == "not-confirmed" && account["qrText"] == "", "not-confirmed is reported without the QR");
    session.Shutdown();
}
void DiagnosisRunsOutsideTheLock() {
    std::atomic<bool> diagnosing{false};
    std::vector<std::string> hosts;
    std::mutex hostsMutex;
    auto options = Fixtures();
    options.beginRetryDelay = 1ms;
    options.execute = [&](Provider, Operation, Json&, const Cancelled&) { return ErrorReplyFixture("network"); };
    options.diagnoseHost = [&](const std::string& host) {
        { std::lock_guard lock(hostsMutex); hosts.push_back(host); }
        diagnosing = true;
        std::this_thread::sleep_for(150ms);
        return host == "userservice.kugou.com" ? std::string("ipv6-failed") : std::string();
    };
    MusicProviderAccountSession session(options);
    session.HandlePage({"begin", Provider::Kugou, 1});
    Require(Await([&] { return diagnosing.load(); }), "self-check started");
    const auto start = std::chrono::steady_clock::now();
    State(session);
    Require(std::chrono::steady_clock::now() - start < 100ms, "page state is not blocked by the self-check");
    Require(Await([&] { return State(session)["accounts"][2]["error"] == "network-ipv6-failed"; }),
        "the first failing login host names the verdict");
    std::lock_guard lock(hostsMutex);
    Require(hosts == std::vector<std::string>{"login-user.kugou.com", "userservice.kugou.com"},
        "self-check probes the provider's real login hosts in order");
    session.Shutdown();
}
void FailedRefreshKeepsCommittedCredentials() {
    std::atomic<int> calls{0};
    auto options = Fixtures();
    options.load = [](Provider) { return Json{{"secret", "fixture"}}; };
    options.execute = [&](Provider, Operation, Json&, const Cancelled&) {
        if (++calls == 2) {
            Reply reply;
            reply.status = "error";
            reply.error = "network";
            return reply;
        }
        return SignedIn();
    };
    MusicProviderAccountSession session(options);
    session.HandlePage({"refresh", Provider::QQ, 1});
    Require(Await([&] { return State(session)["accounts"][1]["status"] == "signed-in"; }), "refresh signed in");
    const auto before = session.AcquireCredentials(Provider::QQ);
    Require(before && before->userId == "123" && session.IsCurrent(*before), "committed credentials usable");
    session.HandlePage({"refresh", Provider::QQ, 2});
    Require(Await([&] { return State(session)["accounts"][1]["status"] == "error"; }), "refresh failed");
    // The account card still shows the committed identity, so reads must keep
    // working with the last known good credentials instead of reporting signed-out.
    const auto after = session.AcquireCredentials(Provider::QQ);
    Require(after && after->userId == "123" && session.IsCurrent(*after), "failed refresh keeps credentials");
    session.HandlePage({"logout", Provider::QQ, 3});
    Require(Await([&] { return State(session)["accounts"][1]["status"] == "signed-out"; }), "logout");
    Require(!session.AcquireCredentials(Provider::QQ), "logout revokes credentials");
    session.Shutdown();
}
// Like Folia's qq-music-api, one device per install: signing out and back in
// must present the same device, not add another "MI 6" to the account.
void SignOutKeepsTheDevice() {
    std::mutex lock;
    Json disk = Json::object();
    std::vector<std::string> devices;
    auto options = Fixtures();
    options.load = [&](Provider) { std::lock_guard<std::mutex> guard(lock); return disk; };
    options.save = [&](Provider, const Json& secrets) { std::lock_guard<std::mutex> guard(lock); disk = secrets; };
    options.clear = [&](Provider) { std::lock_guard<std::mutex> guard(lock); disk = Json::object(); };
    options.execute = [&](Provider, Operation, Json& secrets, const Cancelled&) {
        if (!secrets.contains("device")) secrets["device"] = {{"model", "MI 6"}, {"openUdid", std::to_string(devices.size() + 1)}};
        devices.push_back(secrets["device"]["openUdid"]);
        secrets["secret"] = "session";
        return SignedIn();
    };
    MusicProviderAccountSession session(options);
    session.HandlePage({"refresh", Provider::QQ, 1});
    Require(Await([&] { return State(session)["accounts"][1]["status"] == "signed-in"; }), "first sign-in");
    session.HandlePage({"logout", Provider::QQ, 2});
    Require(Await([&] { return State(session)["accounts"][1]["status"] == "signed-out"; }), "signed out");
    {
        std::lock_guard<std::mutex> guard(lock);
        Require(!disk.contains("secret") && disk.contains("device"), "sign-out drops the session, keeps the device");
    }
    session.HandlePage({"refresh", Provider::QQ, 3});
    Require(Await([&] { return devices.size() >= 2; }), "second sign-in ran");
    Require(devices[0] == devices[1], "the second sign-in reuses the device");
    session.Shutdown();
}
}
int main() {
    try {
        Require(ParseMusicProviderAccountCommand(LR"({"version":1,"scope":"music-provider-account","action":"snapshot"})").has_value(), "snapshot");
        Require(ParseMusicProviderAccountCommand(LR"({"version":1,"scope":"music-provider-account","action":"begin","provider":"qq","request":1})").has_value(), "begin");
        for (const auto* input : {
            LR"({"version":1,"scope":"music-provider-account","action":"begin","provider":"qq","request":0})",
            LR"({"version":1,"scope":"music-provider-account","action":"begin","provider":"qq","request":1.5})",
            LR"({"version":1,"scope":"music-provider-account","action":"begin","provider":"other","request":1})",
            LR"({"version":1,"scope":"music-provider-account","action":"browser","provider":"qq","request":1})",
            LR"({"version":1,"scope":"music-provider-account","action":"begin","provider":"qq","request":1,"cookie":"bad"})",
            LR"({"version":1,"scope":"music-provider-account","action":"begin","provider":"qq","provider":"netease","request":1})"})
            Require(!ParseMusicProviderAccountCommand(input), "invalid command accepted");

        std::atomic<int> polls{0}, writes{0}, clears{0};
        MusicProviderAccountOptions options;
        options.verification = true;
        options.pollInterval = 5ms;
        options.load = [](Provider) { return Json::object(); };
        options.save = [&](Provider provider, const Json& secrets) {
            Require(provider == Provider::QQ && secrets.at("secret") == "native-only", "save scope");
            ++writes;
        };
        options.clear = [&](Provider provider) { Require(provider == Provider::QQ, "clear scope"); ++clears; };
        options.execute = [&](Provider, Operation operation, Json& secrets, const Cancelled&) {
            secrets["secret"] = "native-only";
            Reply reply;
            if (operation == Operation::Begin) {
                reply.status = "authorizing";
                reply.phase = "waiting";
                reply.qrImage = "data:image/png;base64,YQ==";
            } else if (++polls == 1) {
                reply.status = "authorizing";
                reply.phase = "scanned";
            } else {
                reply = SignedIn();
            }
            return reply;
        };
        MusicProviderAccountSession session(options);
        session.HandlePage({"snapshot"});
        Require(Await([&] { return State(session)["accounts"][0]["status"] == "signed-out"; }), "restore");
        session.HandlePage({"begin", Provider::QQ, 1});
        Require(Await([&] { return State(session)["accounts"][1]["status"] == "signed-in"; }), "confirm");
        Require(writes == 1 && polls == 2, "poll once per interval and save once");
        Identity(State(session)["accounts"][1], true);
        const auto state = State(session).dump();
        Require(state.find("native-only") == std::string::npos && State(session)["accounts"][1]["qrImage"] == "", "credential leak");
        session.CancelActive();
        Require(State(session)["accounts"][1]["status"] == "signed-in", "hide logged out");
        Identity(State(session)["accounts"][1], true);
        session.HandlePage({"logout", Provider::QQ, 2});
        Require(Await([&] { return State(session)["accounts"][1]["status"] == "signed-out"; }), "logout");
        Require(clears == 1, "durable logout");
        Identity(State(session)["accounts"][1], false);
        session.Shutdown();

        std::atomic<bool> entered{false}, release{false};
        options.execute = [&](Provider, Operation, Json&, const Cancelled&) {
            entered = true;
            while (!release) std::this_thread::sleep_for(1ms);
            Reply reply = SignedIn();
            reply.userId = "999";
            reply.displayName = "Late";
            reply.avatarUrl = "https://q1.qlogo.cn/late.jpg";
            return reply;
        };
        MusicProviderAccountSession cancellation(options);
        cancellation.HandlePage({"begin", Provider::QQ, 3});
        Require(Await([&] { return entered.load(); }), "request started");
        cancellation.HandlePage({"cancel", Provider::QQ, 4});
        release = true;
        Require(Await([&] { return !cancellation.HasWork() || cancellation.DrainNotification().has_value(); }), "cancel notification");
        cancellation.Shutdown();
        Require(writes == 1 && State(cancellation)["accounts"][1]["status"] == "signed-out", "late auth saved");
        Identity(State(cancellation)["accounts"][1], false);

        options.qrLifetime = 10ms;
        options.execute = [](Provider, Operation, Json&, const Cancelled&) {
            Reply reply;
            reply.status = "authorizing";
            reply.phase = "waiting";
            reply.qrText = "https://example.invalid/fixture";
            return reply;
        };
        MusicProviderAccountSession expiry(options);
        expiry.HandlePage({"begin", Provider::QQ, 5});
        Require(Await([&] { return State(expiry)["accounts"][1]["status"] == "expired"; }), "QR timeout");
        Require(State(expiry)["accounts"][1]["qrText"] == "", "expired QR retained");
        Identity(State(expiry)["accounts"][1], false);
        expiry.Shutdown();
        ShutdownLogout();
        RestoreRetry(false);
        RestoreRetry(true);
        StaleCommands();
        ExpiryBeforeNetwork();
        ExpiryAfterLoad();
        ExpiryDuringRequest(false);
        ExpiryDuringRequest(true);
        ExpiryDuringRequest(true, true);
        ExpiryDuringPollWait();
        LogoutRetry();
        CancelRefresh();
        PublicImageOrigins();
        PublicProfileLimits();
        InvalidPublicProfile();
        CredentialErrorContract();
        RefreshProfile();
        PollRetriesTransientFailures();
        PollFailureCapStillEndsTheAttempt();
        FailedRefreshKeepsCommittedCredentials();
        SignOutKeepsTheDevice();
        BeginRetriesNetworkFailureOnce();
        ScannedButUnconfirmedExpires();
        DiagnosisRunsOutsideTheLock();
        std::cout << "music provider account lifecycle passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
