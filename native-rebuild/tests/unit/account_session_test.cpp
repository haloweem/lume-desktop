#include <iostream>
#include "apps/shell/account_session.h"
#include "tests/unit/support/account_backend_fixture.h"
#include <objbase.h>
#include "unique_test_directory.h"
#include <fstream>
#include <cstdlib>
using namespace lume;
using namespace lume::account;
void Expect(bool ok, const char* why) { if (!ok) { std::cerr << why << '\n'; std::exit(1); } }
void Pump(unsigned milliseconds) {
    const auto end = GetTickCount64() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 2, QS_ALLINPUT);
    } while (GetTickCount64() < end);
}
template<class F> void Until(F done, const char* why) {
    for (int i = 0; i < 300 && !done(); ++i) Pump(10);
    Expect(done(), why);
}
std::string Param(const std::string& url, const char* key) {
    auto begin = url.find(std::string(key) + "=");
    Expect(begin != std::string::npos, "authorization query"); begin += std::strlen(key) + 1;
    return url.substr(begin, url.find('&', begin) - begin);
}
int main() {
    Expect(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "STA");
    {
        shell::AccountSessionOptions options; options.verification = true;
        shell::AccountSession empty(GetModuleHandleW(nullptr), options, {});
        Expect(empty.Restore() && !empty.HasWork(), "verification no ports/credentials never prolongs zero-component exit");
        Expect(StrictJson(Utf8(empty.State()))["profile"].is_null(), "initial null profile");
    }
    auto http = std::make_shared<testing::FixtureTransport>();
    auto store = std::make_shared<testing::MemoryCredentials>();
    std::atomic<std::int64_t> now{1000};
    std::string browser;
    bool selectorFails = true;
    bool protocolWritable = true;
    int protocolCalls = 0, browserCalls = 0;
    const auto files = UniqueTestDirectory(std::filesystem::path(LUME_ACCOUNT_TEST_ARTIFACTS), "account-avatar-");
    const auto selectedFile = files / "selected.png";
    {
        const auto bytes = testing::Png();
        std::ofstream output(selectedFile, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    shell::AccountSessionOptions options;
    options.verification = true; options.transport = http; options.credentials = store; options.clock = [&] { return now.load(); };
    options.openBrowser = [&](std::wstring_view url) {
        ++browserCalls; browser = Utf8(url); http->nonce = Param(browser, "nonce"); return true;
    };
    options.ensureLoginProtocol = [&] { ++protocolCalls; return protocolWritable; };
    options.chooseAvatarFile = [&](HWND) -> std::optional<std::wstring> {
        if (selectorFails) throw Failure(Error::InvalidAvatar);
        return selectedFile.wstring();
    };
    const auto thread = GetCurrentThreadId();
    std::vector<Json> snapshots;
    shell::AccountSession session(GetModuleHandleW(nullptr), options, [&](const std::wstring& text) {
        Expect(GetCurrentThreadId() == thread, "Notify always UI thread");
        auto j = StrictJson(Utf8(text), 3 * MaxAvatarBytes);
        Expect(j.size() == 7 && j["version"] == 1 && j["type"] == "account-state", "fixed snapshot keys");
        Expect((j["status"] == "signed-in") == !j["profile"].is_null(), "profile only in signed-in");
        Expect(text.find(L"fixture-access") == text.npos && text.find(L"fixture-refresh") == text.npos &&
            text.find(L"code_verifier") == text.npos && text.find(L"nonce") == text.npos, "no secrets in public state");
        if (!snapshots.empty()) Expect(j["revision"] > snapshots.back()["revision"], "monotonic revisions");
        snapshots.push_back(std::move(j));
    });
    auto state = [&] { return StrictJson(Utf8(session.State()), 3 * MaxAvatarBytes); };
    Expect(session.Restore(), "restore accepted");
    Until([&] { return !session.HasWork(); }, "empty restore completes");
    Expect(protocolCalls == 0 && browserCalls == 0, "constructor and restore do not register a scheme");
    protocolWritable = false;
    session.HandlePage({ui::AccountAction::Login, {}});
    Expect(protocolCalls == 1 && browserCalls == 0 && state()["status"] == "signed-out" && !session.HasWork(),
        "protocol write failure must not open browser or retain authorization");
    Expect(state()["error"] == "protocol-registration-failed", "safe protocol registration error");
    protocolWritable = true;
    session.HandlePage({ui::AccountAction::Login, {}});
    Expect(session.HasWork() && state()["status"] == "authorizing", "browser transaction keeps app alive");
    Expect(protocolCalls == 2 && browserCalls == 1, "protocol registration precedes browser");
    Expect(!session.HandleCallback(L"lume://login?code=x&state=wrong"), "invalid callback rejected in native layer");
    const auto callback = [&] { return Wide("lume://login?code=fixture-code&state=" + Param(browser, "state")); };
    Expect(session.HandleCallback(callback()), "matched callback queued");
    Until([&] { return state()["status"] == "signed-in" && !state()["busy"].get<bool>(); }, "login result delivered");
    Expect(state()["profile"].size() == 5 && session.HasWork(), "public profile schema and signed-in keepalive");
    Expect(!session.HandleCallback(callback()), "consumed callback rejected");
    session.HandlePage({ui::AccountAction::UpdateName, "Updated Name"});
    Until([&] { return !state()["busy"].get<bool>(); }, "profile update completes");
    Expect(state()["profile"]["displayName"] == "Updated Name", "session dispatches native profile update");
    session.HandlePage({ui::AccountAction::ChooseAvatar, {}});
    Expect(!state()["busy"].get<bool>() && state()["error"] == "invalid-avatar", "selector failure must release busy state");
    selectorFails = false;
    session.HandlePage({ui::AccountAction::ChooseAvatar, {}});
    Until([&] { return !state()["busy"].get<bool>(); }, "selected file upload completes");
    Expect(state()["profile"]["avatarUrl"].get<std::string>().find("data:image/png;base64,") == 0,
        "native selected file read, byte validation, ticket, PUT, profile update reaches snapshot");
    http->deviceInactive = true; now = 1061;
    Until([&] { return state()["status"] == "signed-out"; }, "minute device timer invalidates session");
    Expect(state()["error"] == "device-inactive" && !store->Load() && !session.HasWork(), "device logout clears keepalive and credentials");
    http->deviceInactive = false; store->failSave = true;
    session.HandlePage({ui::AccountAction::Login, {}}); session.HandleCallback(callback());
    Until([&] { return !state()["busy"].get<bool>(); }, "failed-save callback completes");
    Expect(state()["status"] == "signed-out" && state()["error"] == "storage-failed", "save failure cannot report success");
    store->failSave = false;
    http->hold = true; http->ignoreCancellation = true;
    const auto requests = http->tokenRequests.load();
    session.HandlePage({ui::AccountAction::Login, {}}); session.HandleCallback(callback());
    Until([&] { return http->tokenRequests > requests; }, "held token request started");
    session.HandlePage({ui::AccountAction::Logout, {}});
    const auto logoutRevision = state()["revision"].get<std::uint64_t>();
    Expect(state()["status"] == "signed-out" && !store->Load(), "logout immediately removes public and stored account");
    http->hold = false;
    Until([&] { return !session.HasWork(); }, "logout queue drains");
    Expect(state()["error"] == "local-logout" && !store->Load(), "local logout explicitly labeled");
    for (const auto& j : snapshots) if (j["revision"].get<std::uint64_t>() > logoutRevision)
        Expect(j["status"] != "signed-in", "late generation cannot resurrect");
    const auto saveCount = store->saves.load();
    session.HandlePage({ui::AccountAction::Login, {}}); session.HandleCallback(callback());
    // Do not pump the UI: credential save can finish before the posted result is consumed.
    for (int i = 0; i < 400 && store->saves == saveCount; ++i) Sleep(5);
    Expect(store->saves > saveCount, "worker committed before UI notification fixture");
    session.HandlePage({ui::AccountAction::CancelLogin, {}});
    Expect(!store->Load(), "cancel pending delivery clears just-committed credentials");
    Until([&] { return !session.HasWork(); }, "cancel cleanup drains");
    Expect(state()["status"] == "signed-out", "cancel cannot deliver pending signed-in result");
    http->hold = true;
    const auto beforeShutdown = http->tokenRequests.load();
    session.HandlePage({ui::AccountAction::Login, {}}); session.HandleCallback(callback());
    Until([&] { return http->tokenRequests > beforeShutdown; }, "uncooperative port running");
    const auto start = GetTickCount64(); session.Shutdown();
    Expect(GetTickCount64() - start < 500 && !session.HasWork(), "shutdown bounded even for broken injected port");
    const auto notifications = snapshots.size();
    http->hold = false; Pump(200);
    Expect(snapshots.size() == notifications && !store->Load(), "detached stale worker cannot access UI or save credentials");
    session.HandlePage({ui::AccountAction::Login, {}});
    Expect(!session.HasWork(), "shutdown cannot be reversed");
    std::filesystem::remove_all(files);
    CoUninitialize();
    std::cout << "account session UI thread, timer, cancellation, bounded shutdown passed\n";
}
