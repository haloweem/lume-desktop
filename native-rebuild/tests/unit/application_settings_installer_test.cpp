#include "apps/shell/application_settings_session.h"
#include "platform/windows/application_settings/application_settings_ports.h"
#include "vendor/nlohmann/json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <atomic>
using namespace lume::application_settings;
using namespace lume::shell;
using lume::ui::ApplicationSettingsAction;
namespace windows = lume::platform::windows;
namespace {
void Check(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
const std::string manifest = R"({"version":"1.0.9","runtime":"native-win32","channel":"native-verification","downloadUrl":"https://static.lumedesktop.com/lume/downloads/LumeSetup-1.0.9.exe","sha256":"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","size":3})";
struct File final : IPreferencesFile {
    FileRead Read() override { return {ReadStatus::Loaded, EncodePreferences({"en", true, false})}; }
    bool WriteAtomically(const std::string&) override { return true; }
};
struct Startup final : IStartupRegistration {
    bool Read(StartupEntry& out) override { out = {}; return true; }
    bool Write(const StartupEntry&) override { return true; }
};
struct Transport final : IUpdateTransport {
    ManifestResponse Fetch(const Cancellation&) override { return {manifest, {}}; }
};
struct Installer final : IUpdateInstaller {
    std::atomic<int> downloads{0}, launches{0};
    bool failDownload = false, failLaunch = false;
    std::atomic<bool> pause{false}, entered{false};
    PreparedUpdate Download(const ManifestResult& release, const Cancellation& cancel, const std::function<void(int)>& progress) override {
        ++downloads; entered = true;
        Check(release.installer.size == 3, "only the native validated descriptor reaches the downloader");
        progress(33);Sleep(30);
        while (pause && !cancel.cancelled) Sleep(1);
        if (cancel.cancelled) return {{}, "update-cancelled"};
        if (failDownload) return {{}, "update-checksum"};
        progress(100);return {L"C:\\Temp\\verified-fixture.exe", {}};
    }
    std::string Launch(const PreparedUpdate& prepared, const ManifestResult&, const std::wstring& directory) override {
        ++launches;
        Check(prepared.path == L"C:\\Temp\\verified-fixture.exe" && directory == L"C:\\Lume",
              "installer uses only the prepared file and the existing application directory");
        return failLaunch ? "update-install-cancelled" : "";
    }
};
template<class Predicate> bool Pump(Predicate done) {
    const auto deadline = GetTickCount64() + 5000;
    do {
        MSG msg{};while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {TranslateMessage(&msg);DispatchMessageW(&msg);}
        if (done()) return true;
        Sleep(1);
    } while (GetTickCount64() < deadline);
    return false;
}
void Pipeline(bool failedDownload, bool failedLaunch, bool cancel) {
    auto installer = std::make_shared<Installer>();
    installer->failDownload = failedDownload;installer->failLaunch = failedLaunch;installer->pause = cancel;
    ApplicationSettingsOptions options;options.verification = true;
    options.testPorts = {std::make_shared<File>(),std::make_shared<Startup>(),std::make_shared<Transport>(),L"C:\\Lume\\LumeShell.exe",installer};
    bool sawProgress = false;
    ApplicationSettingsSession session(nullptr, options, [&](const std::wstring& text) {
        const auto json = nlohmann::json::parse(text);
        sawProgress |= json["update"]["status"] == "downloading" && json["update"]["progress"] == 33;
    }, [](bool) {return true;});
    Check(session.RequestUpdateCheck(true) && Pump([&] {return !session.HasWork();}), "manifest check completes");
    Check(nlohmann::json::parse(session.State())["update"]["status"] == "available", "available action is public");
    session.HandlePage({ApplicationSettingsAction::DownloadUpdate});
    if (cancel) {
        Check(Pump([&] {return installer->entered.load();}), "download starts before cancellation");
        session.JoinShutdown();Check(installer->launches == 0, "shutdown never launches an installer");return;
    }
    Check(Pump([&] {return !session.HasWork();}), "download/install completion returns to UI");
    const auto state = nlohmann::json::parse(session.State());
    Check(sawProgress && installer->downloads == 1, "native progress is delivered without duplicate downloads");
    if (failedDownload) Check(installer->launches == 0 && state["error"] == "update-checksum", "failed verification never launches");
    else if (failedLaunch) {
        Check(installer->launches == 1 && state["update"]["status"] == "ready", "cancelled UAC retains verified file for retry");
        installer->failLaunch = false;session.HandlePage({ApplicationSettingsAction::InstallUpdate});
        Check(Pump([&] {return !session.HasWork();}) && installer->downloads == 1 && installer->launches == 2,
              "retry installation reuses the verified package");
    } else Check(installer->launches == 1 && state["update"]["status"] == "installing", "explicit download action launches installation after verification");
    session.JoinShutdown();
}
}
int main(int argc, char** argv) {
    try {
        const auto release = EvaluateManifest(manifest);
        Check(release.update.status == "available", "test manifest is valid");
        const auto root = std::filesystem::temp_directory_path() / ("LumeUpdateTest-" + std::to_string(GetCurrentProcessId()));
        std::filesystem::create_directory(root);
        const auto path = root / "fixture.exe";
        {std::ofstream file(path, std::ios::binary);file << "abc";}
        Check(windows::VerifyApplicationUpdateFile(path.wstring(), release), "valid file passes size and SHA256");
        {std::ofstream file(path, std::ios::binary);file << "abd";}
        Check(!windows::VerifyApplicationUpdateFile(path.wstring(), release), "same-size corruption fails verification");
        {std::ofstream file(path, std::ios::binary);file << "abcd";}
        Check(!windows::VerifyApplicationUpdateFile(path.wstring(), release), "wrong length fails verification");
        std::filesystem::remove(path);std::filesystem::remove(root);
        auto inert = windows::CreateApplicationUpdateInstaller(true);Cancellation cancellation;
        Check(inert->Download(release, cancellation, {}).error == "update-verification" &&
              inert->Launch({L"C:\\unknown.exe",{}}, release,L"C:\\Lume") == "update-verification", "verification adapter never downloads or runs software");
        Pipeline(false,false,false);Pipeline(true,false,false);Pipeline(false,true,false);Pipeline(false,false,true);
        if (argc > 1 && std::string(argv[1]) == "--live") {
            auto live = release;
            live.installer = {"https://static.lumedesktop.com/lume/downloads/LumeSetup-1.0.3.1.exe",
                "4a0edeadd4611e5c89e0e38323765705afb15e50347e0b80359d02f2cda29c10",44139762};
            auto downloader = windows::CreateApplicationUpdateInstaller(false);
            const auto result = downloader->Download(live,cancellation,{});
            if (!result.error.empty()) std::cerr << "LIVE_ERROR=" << result.error << '\n';
            Check(result.error.empty() && windows::VerifyApplicationUpdateFile(result.path,live), "real CDN native downloader verifies the published package");
            std::cout << "LIVE download and hash verification passed; installer was not launched\n";
        }
        std::cout << "native direct update pipeline passed\n";return 0;
    } catch(const std::exception& error) {std::cerr << "FAIL: " << error.what() << '\n';return 1;}
}
