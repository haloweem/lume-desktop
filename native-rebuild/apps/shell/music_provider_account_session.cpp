#include "apps/shell/music_provider_account_session.h"
#include "platform/windows/music_provider_credentials.h"
#include "platform/windows/music_provider_http.h"
#include "platform/windows/music_login_diagnosis.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace lume::shell {
namespace {
// Login hosts used by the self-check, in the order a sign-in reaches them (as
// Folia's SELF_CHECK_HOSTS): the QR fetch first, then what confirming needs.
std::vector<std::string> LoginHosts(music::auth::Provider provider) {
    switch (provider) {
    case music::auth::Provider::QQ: return {"ssl.ptlogin2.qq.com", "u.y.qq.com", "api.tencentmusic.com"};
    case music::auth::Provider::Kugou: return {"login-user.kugou.com", "userservice.kugou.com", "gateway.kugou.com"};
    default: return {"interfacepc.music.163.com", "music.163.com"};
    }
}
}
namespace {
using namespace music::auth;
constexpr std::uint64_t MaxCounter = 9007199254740991ULL;
constexpr std::array<Provider, 3> Providers{Provider::Netease, Provider::QQ, Provider::Kugou};
// The device each provider sees (QQ: "device", the MI 6 profile; Kugou: KUGOU_API_*;
// NetEase: deviceId). Like Folia's qq-music-api, which keeps its device in a state
// file apart from the login, it belongs to this install and outlives sign-outs:
// minting a new one per sign-in adds another device to the account every time.
constexpr const char* IdentityKeys[] = {
    "device", "deviceId", "KUGOU_API_GUID", "KUGOU_API_MID", "KUGOU_API_DEV",
    "KUGOU_API_MAC", "KUGOU_API_WEBGL", "KUGOU_API_PLATFORM", "dfid"};
Json DeviceIdentity(const Json& secrets) {
    Json identity = Json::object();
    if (!secrets.is_object()) return identity;
    for (const auto* key : IdentityKeys)
        if (secrets.contains(key) && !secrets[key].is_null() && !secrets[key].empty()) identity[key] = secrets[key];
    return identity;
}
// Stored secrets that hold only the device: nothing to restore a session from.
bool DeviceOnly(const Json& secrets) {
    return secrets.is_object() && secrets.size() == DeviceIdentity(secrets).size();
}
std::wstring Wide(const std::string& text) {
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), nullptr, 0);
    if (!size) throw std::runtime_error("invalid-response");
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), result.data(), size);
    return result;
}
bool PublicText(const std::string& text, std::size_t limit) {
    return text.size() <= limit &&
        std::none_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c == 127; }) &&
        (text.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
            static_cast<int>(text.size()), nullptr, 0) != 0);
}
bool PublicReply(const Reply& reply) {
    const std::set<std::string> statuses{"signed-out", "authorizing", "signed-in", "expired", "error"};
    const std::set<std::string> kinds{"qq-green-vip", "qq-super-vip", "netease-vip", "netease-svip", "kugou-vip"};
    const std::set<std::string> errors{"", "network", "service", "invalid-response", "storage", "expired", "cancelled",
        "qq-status-response", "qq-authorization-response", "qq-device-response", "qq-credential-response", "qq-profile-response",
        "rate-limited", "not-confirmed"};
    const auto credentialError = [&] {
        const auto& error = reply.error;
        // Login self-check verdicts are short tokens such as network-ipv6-failed.
        if (error.rfind("network-", 0) == 0 && error.size() <= 32) return true;
        for (const auto* fixed : {"qq-credential-network", "qq-credential-malformed",
                                  "qq-credential-missing-id", "qq-credential-missing-key", "qq-credential-expired"})
            if (error == fixed) return true;
        for (const auto* prefix : {"qq-credential-http-", "qq-credential-code-", "netease-code-"}) {
            const std::string_view value(error);
            const std::string_view start(prefix);
            if (value.substr(0, start.size()) != start) continue;
            const auto digits = value.substr(start.size());
            if (start == "qq-credential-http-" ? digits.size() != 3 : digits.empty() || digits.size() > 8)
                return false;
            return std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return c >= '0' && c <= '9'; });
        }
        return false;
    };
    if (!statuses.count(reply.status) || (!errors.count(reply.error) && !credentialError()) ||
        (reply.phase != "" && reply.phase != "waiting" && reply.phase != "scanned") ||
        !PublicText(reply.userId, 128) || !PublicText(reply.displayName, 256) ||
        reply.qrImage.size() > 350000 || !PublicText(reply.qrText, 2048) ||
        !PublicImageUrl(reply.avatarUrl) || reply.memberships.size() > 8) return false;
    for (const auto& membership : reply.memberships) {
        if (!kinds.count(membership.kind) || membership.label.empty() ||
            !PublicText(membership.kind, 64) || !PublicText(membership.label, 128) ||
            !PublicImageUrl(membership.iconUrl) || membership.level < 0 || membership.level > 1000 || membership.expiresAt < 0 ||
            static_cast<std::uint64_t>(membership.expiresAt) > MaxCounter) return false;
    }
    if (!reply.qrImage.empty()) {
        const std::string prefix = "data:image/png;base64,";
        if (reply.qrImage.compare(0, prefix.size(), prefix) != 0 ||
            reply.qrImage.find_first_not_of(
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=", prefix.size()) != std::string::npos) return false;
    }
    return reply.status != "signed-in" || (!reply.userId.empty() && !reply.displayName.empty());
}
Reply ErrorReply(std::string error) {
    Reply reply;
    reply.status = "error";
    reply.error = std::move(error);
    return reply;
}
// A transient poll failure must not consume the QR session. QQ reports the
// pre-consent status stage separately because later stages consume the code.
bool RetryablePoll(std::string_view error) {
    return error == "network" || error == "service" || error == "qq-status-response";
}
}

std::optional<MusicProviderAccountCommand> ParseMusicProviderAccountCommand(std::wstring_view input) noexcept {
    try {
        if (input.empty() || input.size() > 2048) return {};
        bool invalid = false;
        std::set<std::string> keys;
        const auto json = Json::parse(input.begin(), input.end(),
            [&](int depth, Json::parse_event_t event, Json& value) {
                if (depth > 2) invalid = true;
                if (event == Json::parse_event_t::key && !keys.insert(value.get<std::string>()).second) invalid = true;
                return true;
            }, false);
        if (invalid || !json.is_object() || !json.contains("version") || !json.at("version").is_number_integer() ||
            json.value("version", 0) != 1 ||
            json.value("scope", "") != "music-provider-account") return {};
        MusicProviderAccountCommand command;
        command.action = json.at("action").get<std::string>();
        if (command.action == "snapshot") return json.size() == 3 ? std::optional{command} : std::nullopt;
        if (command.action != "begin" && command.action != "cancel" &&
            command.action != "refresh" && command.action != "logout") return {};
        if (json.size() != 5 || !json.at("request").is_number_unsigned()) return {};
        command.request = json.at("request").get<std::uint64_t>();
        if (!command.request || command.request > MaxCounter) return {};
        const auto name = json.at("provider").get<std::string>();
        for (const auto provider : Providers) if (name == Name(provider)) {
            command.provider = provider;
            return command;
        }
    } catch (...) {}
    return {};
}

struct MusicProviderAccountSession::Impl {
    using Clock = std::chrono::steady_clock;
    struct Entry {
        std::atomic<std::uint64_t> generation{0};
        std::uint64_t request = 0;
        Reply reply, committed;
        bool initialized = false, pending = false, polling = false;
        int pollFailures = 0;
        std::string action;
        Clock::time_point due{}, expires{}, scannedAt{};
        // Accessed only by the worker, never by page handlers.
        Json transient = Json::object();
    };
    explicit Impl(MusicProviderAccountOptions value) : options(std::move(value)) {}
    ~Impl() { Shutdown(); }
    void ChangedLocked() {
        revision = revision == MaxCounter ? 1 : revision + 1;
        notification = true;
        if (!closed && options.wake) options.wake();
    }
    void StartLocked() {
        if (worker.joinable() && finished) worker.join();
        if (!worker.joinable()) {
            finished = false;
            worker = std::thread([this] { Run(); });
        }
        condition.notify_all();
    }
    void CancelLocked(Entry& entry) {
        if (entry.action == "restore" && (entry.pending || entry.reply.status == "loading"))
            entry.initialized = false;
        ++entry.generation;
        entry.pending = entry.polling = false;
        entry.pollFailures = 0;
        entry.scannedAt = {};
        entry.reply = entry.committed;
    }
    void CancelActive() {
        std::lock_guard lock(mutex);
        if (closed) return;
        bool changed = false;
        for (auto& entry : entries) {
            // Logout is a durable explicit operation, not a modal operation.
            if (entry.action == "logout") continue;
            if (entry.pending || entry.polling || entry.reply.status == "loading" || entry.reply.status == "authorizing") {
                CancelLocked(entry);
                changed = true;
            }
        }
        if (changed) ChangedLocked();
        condition.notify_all();
    }
    // The last committed identity stays usable while a refresh is in flight or
    // after it failed transiently. Only a new QR login (switching accounts), an
    // explicit logout, or a server-confirmed expiry revokes the credentials.
    bool IdentityAvailableLocked(const Entry& entry) const {
        if (entry.committed.status != "signed-in") return false;
        if (entry.reply.status == "signed-in") return true;
        return entry.action == "refresh" &&
            (entry.reply.status == "loading" || entry.reply.status == "error");
    }
    std::wstring HandlePage(const MusicProviderAccountCommand& command) {
        std::lock_guard lock(mutex);
        if (closed) return StateLocked();
        if (command.action == "snapshot") {
            bool scheduled = false;
            for (auto& entry : entries) if (!entry.initialized) {
                entry.initialized = true;
                entry.action = "restore";
                entry.pending = true;
                entry.reply.status = "loading";
                ++entry.generation;
                scheduled = true;
            }
            if (scheduled) { ChangedLocked(); StartLocked(); }
            return StateLocked();
        }
        const auto index = static_cast<std::size_t>(command.provider);
        if (index >= entries.size() || !command.request || command.request > MaxCounter) return StateLocked();
        auto& entry = entries[index];
        if (command.request <= entry.request) return StateLocked();
        if (entry.action == "logout" && entry.reply.status == "loading")
            return StateLocked();
        if (command.action == "begin") {
            for (auto& other : entries) if (&other != &entry &&
                (other.polling || (other.action == "begin" && other.reply.status == "loading"))) CancelLocked(other);
        } else if (command.action != "cancel" && command.action != "logout" && command.action != "refresh") {
            return StateLocked();
        }
        CancelLocked(entry);
        entry.request = command.request;
        if (command.action != "cancel") entry.initialized = true;
        entry.action = command.action;
        if (command.action != "cancel") {
            entry.pending = true;
            entry.reply.status = "loading";
            entry.reply.error.clear();
            entry.expires = Clock::now() + options.qrLifetime;
            StartLocked();
        }
        ChangedLocked();
        condition.notify_all();
        return StateLocked();
    }
    std::wstring StateLocked() const {
        Json accounts = Json::array();
        for (std::size_t index = 0; index < entries.size(); ++index) {
            const auto& entry = entries[index];
            const auto& reply = entry.reply;
            accounts.push_back({{"provider", Name(Providers[index])}, {"request", entry.request},
                {"status", reply.status}, {"phase", reply.phase}, {"userId", reply.userId},
                {"displayName", reply.displayName}, {"qrImage", reply.qrImage},
                {"qrText", reply.qrText}, {"error", reply.error},
                {"avatarUrl", reply.avatarUrl}, {"memberships", reply.memberships}});
        }
        return Wide(Json{{"version", 1}, {"type", "music-provider-account-state"},
            {"revision", revision}, {"accounts", std::move(accounts)}}.dump());
    }
    Json Load(Provider provider) {
        if (options.load) return options.load(provider);
        if (options.verification || options.root.empty()) return Json::object();
        return LoadCredentials(options.root, provider);
    }
    void Save(Provider provider, const Json& secrets) {
        if (options.save) options.save(provider, secrets);
        else if (!options.verification && !options.root.empty()) SaveCredentials(options.root, provider, secrets);
        else throw std::runtime_error("storage");
    }
    void Clear(Provider provider) {
        if (options.clear) options.clear(provider);
        else if (!options.verification && !options.root.empty()) ClearCredentials(options.root, provider);
    }
    // Drops the session but keeps this install's device for the next sign-in.
    void SignOut(Provider provider, const Json& known) {
        Json identity = DeviceIdentity(known);
        try {
            const auto stored = DeviceIdentity(Load(provider));
            for (auto item = stored.begin(); item != stored.end(); ++item) identity[item.key()] = item.value();
        } catch (...) {}
        if (identity.empty()) Clear(provider);
        else Save(provider, identity);
    }
    void RunOne(std::size_t index, std::uint64_t generation, const std::string& action, Clock::time_point expires) {
        auto& entry = entries[index];
        const auto provider = Providers[index];
        const auto superseded = [&] {
            return (action != "logout" && stopped.load()) || entry.generation.load() != generation;
        };
        const auto expired = [&] {
            return (action == "begin" || action == "poll") && Clock::now() >= expires;
        };
        const Cancelled cancelled = [&] { return superseded() || expired(); };
        Reply reply;
        bool storage = false;
        try {
            CheckCancelled(cancelled);
            if (action != "logout") {
                if (action != "poll") {
                    storage = true;
                    try { entry.transient = Load(provider); }
                    catch (...) {
                        if (action != "begin") throw;
                        entry.transient = Json::object();
                    }
                    storage = false;
                }
                CheckCancelled(cancelled);
                if (action == "restore" && (entry.transient.empty() || DeviceOnly(entry.transient))) reply = {};
                else if (options.verification && !options.execute) reply = ErrorReply("service");
                else {
                    const auto operation = action == "begin" ? Operation::Begin :
                        action == "poll" ? Operation::Poll : Operation::Refresh;
                    const auto run = [&] {
                        return options.execute ? options.execute(provider, operation, entry.transient, cancelled) :
                            ExecuteProvider(provider, operation, entry.transient, cancelled);
                    };
                    reply = run();
                    // Folia's withQrNetworkRetry: a QR fetch that failed at the network
                    // level is tried once more after a short pause before it is shown.
                    if (action == "begin" && reply.status == "error" && reply.error == "network") {
                        for (auto until = Clock::now() + options.beginRetryDelay; Clock::now() < until;) {
                            CheckCancelled(cancelled);
                            std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        }
                        CheckCancelled(cancelled);
                        reply = run();
                    }
                }
                if (!PublicReply(reply)) throw std::runtime_error("invalid-response");
                if (action == "begin" && reply.status == "authorizing" &&
                    (reply.qrImage.empty() == reply.qrText.empty())) throw std::runtime_error("invalid-response");
                if (reply.status == "signed-in") {
                    reply.qrImage.clear();
                    reply.qrText.clear();
                    reply.phase.clear();
                }
            }
        } catch (const std::exception& failure) {
            if (superseded()) return;
            const std::string error = failure.what();
            reply = ErrorReply(storage ? "storage" :
                error == "network" || error == "service" ? error : "invalid-response");
        } catch (...) { reply = ErrorReply(storage ? "storage" : "invalid-response"); }
        // Folia-style self-check: when the QR fetch fails on the network, name the
        // broken link instead of a generic error. It probes for seconds, so it runs
        // before taking the lock the page handlers share.
        if (action == "begin" && reply.status == "error" && reply.error == "network" &&
            options.diagnoseHost && !superseded()) {
            for (const auto& host : LoginHosts(provider)) {
                if (superseded()) return;
                const auto verdict = options.diagnoseHost(host);
                if (!verdict.empty()) { reply.error = "network-" + verdict; break; }
            }
        }
        std::lock_guard lock(mutex);
        if (superseded()) return;
        if (expired()) {
            reply = {};
            reply.status = "expired";
            reply.error = "expired";
        }
        // Folia's expired-after-scan: a code scanned but never confirmed on the
        // phone ends after a minute instead of waiting out the whole QR lifetime.
        if (action == "poll" && reply.status == "authorizing") {
            if (reply.phase != "scanned") entry.scannedAt = {};
            else if (entry.scannedAt == Clock::time_point{}) entry.scannedAt = Clock::now();
            else if (Clock::now() - entry.scannedAt >= options.scannedLifetime) {
                reply = {};
                reply.status = "expired";
                reply.error = "not-confirmed";
            }
        }
        // Keep the visible QR alive across a transient poll failure and retry
        // with a bounded backoff. Otherwise one network hiccup forces the user
        // to scan a brand new code.
        if (action == "poll" && reply.status == "error" && RetryablePoll(reply.error)) {
            if (entry.pollFailures < 3) {
                ++entry.pollFailures;
                entry.polling = true;
                auto delay = options.pollInterval * (1 << (entry.pollFailures - 1));
                if (delay > std::chrono::milliseconds(5000)) delay = std::chrono::milliseconds(5000);
                entry.due = std::min(expires, Clock::now() + delay);
                return;
            }
        }
        // A completed attempt (success, expiry, or exhausted retries) starts the
        // next failure budget from zero.
        entry.pollFailures = 0;
        if (action == "refresh" && entry.committed.status == "signed-in") {
            if (reply.status == "error") {
                const auto error = reply.error;
                reply = entry.committed;
                reply.status = "error";
                reply.error = error;
            } else if (reply.status == "signed-in" && reply.userId == entry.committed.userId) {
                if (reply.avatarUrl.empty()) reply.avatarUrl = entry.committed.avatarUrl;
                if (reply.memberships.empty()) reply.memberships = entry.committed.memberships;
            }
        }
        // Commit storage and the visible identity together. A cancelled or
        // replaced attempt cannot recreate credentials after logout.
        try {
            if (action == "logout") SignOut(provider, entry.transient);
            else if (reply.status == "signed-in") Save(provider, entry.transient);
            else if ((action == "restore" || action == "refresh") &&
                     !entry.transient.empty() &&
                     (reply.status == "expired" || reply.status == "signed-out")) SignOut(provider, entry.transient);
            else if (action == "begin" || action == "poll") {
                // A sign-in that has not finished yet still keeps the device it was given,
                // so a retry or an update presents the same one.
                const auto identity = DeviceIdentity(entry.transient);
                if (!identity.empty()) {
                    auto stored = Load(provider);
                    if (!stored.is_object()) stored = Json::object();
                    bool changed = false;
                    for (auto item = identity.begin(); item != identity.end(); ++item)
                        if (!stored.contains(item.key()) || stored[item.key()] != item.value()) {
                            if (stored.contains(item.key()) && !stored[item.key()].empty()) continue;
                            stored[item.key()] = item.value(); changed = true;
                        }
                    if (changed) Save(provider, stored);
                }
            }
        } catch (...) {
            reply = action == "logout" || (action == "refresh" && entry.committed.status == "signed-in")
                ? entry.committed : Reply{};
            reply.status = "error";
            reply.error = "storage";
        }
        if (reply.status == "authorizing") {
            if (reply.qrImage.empty()) reply.qrImage = entry.reply.qrImage;
            if (reply.qrText.empty()) reply.qrText = entry.reply.qrText;
            entry.polling = true;
            entry.due = std::min(expires, Clock::now() + options.pollInterval);
        }
        if (reply.status != "authorizing") {
            entry.polling = false;
            reply.qrImage.clear();
            reply.qrText.clear();
            reply.phase.clear();
            entry.transient = Json::object();
        }
        if (reply.status == "signed-in" || reply.status == "signed-out" || action == "logout" ||
            ((action == "restore" || action == "refresh") && reply.status == "expired")) {
            if (reply.status != "error") entry.committed = reply;
        }
        entry.reply = std::move(reply);
        ChangedLocked();
    }
    void Run() {
        for (;;) {
            std::unique_lock lock(mutex);
            std::size_t index = entries.size();
            auto next = Clock::time_point::max();
            for (std::size_t i = 0; i < entries.size(); ++i) {
                auto& entry = entries[i];
                if (closed && entry.action != "logout") continue;
                if (entry.pending || (entry.polling && entry.due <= Clock::now())) { index = i; break; }
                if (entry.polling) next = std::min(next, entry.due);
            }
            if (index == entries.size()) {
                if (next == Clock::time_point::max()) {
                    for (auto& idle : entries) idle.transient = Json::object();
                    finished = true;
                    return;
                }
                condition.wait_until(lock, next);
                continue;
            }
            auto& entry = entries[index];
            const auto action = entry.pending ? entry.action : "poll";
            const auto generation = entry.generation.load();
            const auto expires = entry.expires;
            entry.pending = false;
            lock.unlock();
            RunOne(index, generation, action, expires);
        }
    }
    void Shutdown() {
        std::unique_lock lock(mutex);
        if (closed) return;
        closed = true;
        stopped = true;
        // Accepted logouts remain runnable after network work is cancelled.
        for (auto& entry : entries) if (entry.action != "logout") CancelLocked(entry);
        condition.notify_all();
        auto retiring = std::move(worker);
        lock.unlock();
        if (retiring.joinable()) retiring.join();
    }
    MusicProviderAccountOptions options;
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::array<Entry, 3> entries;
    std::thread worker;
    std::atomic<bool> stopped{false};
    std::uint64_t revision = 0;
    bool notification = false, closed = false, finished = true;
};

MusicProviderAccountSession::MusicProviderAccountSession(MusicProviderAccountOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}
MusicProviderAccountSession::~MusicProviderAccountSession() = default;
std::wstring MusicProviderAccountSession::HandlePage(const MusicProviderAccountCommand& command) { return impl_->HandlePage(command); }
std::wstring MusicProviderAccountSession::State() const { std::lock_guard lock(impl_->mutex); return impl_->StateLocked(); }
std::optional<std::wstring> MusicProviderAccountSession::DrainNotification() {
    std::lock_guard lock(impl_->mutex);
    if (!impl_->notification || impl_->closed) return {};
    impl_->notification = false;
    return impl_->StateLocked();
}
void MusicProviderAccountSession::CancelActive() { impl_->CancelActive(); }
void MusicProviderAccountSession::Shutdown() { impl_->Shutdown(); }
bool MusicProviderAccountSession::HasWork() const {
    std::lock_guard lock(impl_->mutex);
    return !impl_->closed && (!impl_->finished || impl_->notification);
}
std::optional<MusicProviderCredentialLease> MusicProviderAccountSession::AcquireCredentials(Provider provider) const {
    MusicProviderCredentialLease lease;
    lease.provider = provider;
    {
        std::lock_guard lock(impl_->mutex);
        const auto index = static_cast<std::size_t>(provider);
        if (impl_->closed || index >= impl_->entries.size()) return {};
        const auto& entry = impl_->entries[index];
        if (!impl_->IdentityAvailableLocked(entry)) return {};
        lease.generation = entry.generation.load();
        lease.userId = entry.committed.userId;
    }
    try {
        lease.credentials = impl_->Load(provider);
        if (!lease.credentials.is_object() || lease.credentials.empty() || !IsCurrent(lease)) return {};
        return lease;
    } catch (...) { return {}; }
}
bool MusicProviderAccountSession::IsCurrent(const MusicProviderCredentialLease& lease) const {
    std::lock_guard lock(impl_->mutex);
    const auto index = static_cast<std::size_t>(lease.provider);
    if (impl_->closed || index >= impl_->entries.size()) return false;
    const auto& entry = impl_->entries[index];
    return entry.generation.load() == lease.generation && impl_->IdentityAvailableLocked(entry) &&
        entry.committed.userId == lease.userId;
}
}  // namespace lume::shell
