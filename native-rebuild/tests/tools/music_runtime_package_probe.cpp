#include "platform/windows/folia_runtime_download.h"
#include "platform/windows/folia_music_runtime.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    using namespace lume::platform::windows;
    using namespace lume::music::auth;
    try {
        if (argc < 4) throw std::runtime_error("expected mode, application directory and isolated accounts directory");
        const auto application = std::filesystem::absolute(argv[2]);
        const auto accounts = std::filesystem::absolute(argv[3]);
        const bool available = FoliaRuntimeAvailable(application, accounts);
        std::cout << Json{{"bundledAvailable", available}}.dump() << std::endl;
        if (std::string(argv[1]) == "check") return available ? 0 : 2;
        if (!available) throw std::runtime_error("bundled-runtime-rejected");
        bool downloaded = false;
        const auto selected = EnsureFoliaRuntime(application, accounts, {}, [&](auto) { downloaded = true; });
        if (selected != application / "music-runtime" || downloaded) throw std::runtime_error("unexpected-download-fallback");
        std::cout << Json{{"selectedBundled", true}, {"downloadAttempted", false}}.dump() << std::endl;
        FoliaMusicRuntime runtime(accounts);
        runtime.Prepare();
        for (const auto provider : {Provider::QQ, Provider::Netease}) {
            const auto reply = runtime.Request("auth", provider, {{"operation", "begin"}, {"attempt", 1}});
            const bool ready = reply.value("status", "") == "authorizing" && !reply.value("qrImage", "").empty();
            std::cout << Json{{"provider", Name(provider)}, {"qrReady", ready}}.dump() << std::endl;
            runtime.CancelLogin(provider, 1);
            if (!ready) throw std::runtime_error("qr-not-ready");
        }
        runtime.Shutdown();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << Json{{"error", error.what()}}.dump() << std::endl;
        return 1;
    }
}
