#include "core/application_settings/application_settings.h"
#include <cstdlib>
#include <iostream>
#include <string>
using namespace lume::application_settings;
namespace {
void Check(bool ok, const char* reason) {
    if (!ok) { std::cerr << "FAIL: " << reason << '\n'; std::exit(1); }
}
}
int main() {
    Check(std::string(CurrentVersion) == "1.0.8", "new workspace identifies the 1.0.8 release");
    Check(CompareVersions(CurrentVersion,"1.0.3.3")==1,"1.0.8 supersedes four-segment 1.0.3.3");
    Check(CompareVersions("1.0.3.2", "1.0.2.1") == 1, "release is newer than the accepted baseline");
    Check(CompareVersions("1.0.3.2", "1.0.3") == 1, "fourth version segment identifies the patch release");
    Check(CompareVersions("1.0.2.2", "1.0.2.1") == 1, "compare the fourth version segment");
    Check(CompareVersions("1.0.2", "1.0.2.1") == -1, "missing fourth segment is zero");
    Check(CompareVersions("1.0", "1.0.0.0") == 0, "zero padding is numerically equivalent");
    Check(CompareVersions("1.10.0", "1.9.99") == 1, "numeric not lexical version comparison");
    for (const auto* bad : {"", "1", "1.", "1..2", "1.2.3.4.5", "1.2-beta", "-1.2", " 1.2", "1.2e2", "999999999999999999999.0"})
        Check(!CompareVersions(bad, CurrentVersion), "reject malformed or overflowing versions");
    Check(EvaluateManifest(R"({"version":"1.0.8"})").update.status == "current", "current native version does not require an installer");
    Check(EvaluateManifest(R"({"version":"1.0.2.1"})").update.status == "current", "accepted previous native version does not require an installer");
    Check(EvaluateManifest(R"({"version":"1.0.1","downloads":{}})").update.status == "current",
          "older official Electron manifest is current, without requiring irrelevant native download fields");
    Check(EvaluateManifest(R"({"version":"1.0.10","downloadUrl":"https://lumedesktop.com/old-electron.exe"})").update.status == "incompatible",
          "new Electron package without explicit native identity is blocked");
    Check(EvaluateManifest(R"({"version":"1.0.10","runtime":"electron","channel":"native-verification"})").update.status == "incompatible",
          "wrong runtime blocked");
    Check(EvaluateManifest(R"({"version":"1.0.10","runtime":"native-win32","channel":"stable"})").update.status == "incompatible",
          "wrong native channel blocked");
    for (const auto* body : {
        R"({"version":"1.0.10","runtime":null,"channel":"native-verification"})",
        R"({"version":"1.0.10","runtime":["native-win32"],"channel":"native-verification"})",
        R"({"version":"1.0.10","runtime":"native-win32","channel":1})"
    }) Check(EvaluateManifest(body).update.status == "incompatible", "new version requires explicit string identity, never a coerced one");
    const std::string valid = R"({"version":"1.0.8","runtime":"native-win32","channel":"native-verification","downloadUrl":"https://www.lumedesktop.com/LumeSetup-1.0.8.exe","sha256":")" +
        std::string(64, 'a') + R"(","size":1024})";
    const auto release = EvaluateManifest(valid, "1.0.7");
    Check(release.update.status == "available" && release.update.version == "1.0.8" && release.error.empty() && release.installer.size == 1024,
          "valid native metadata exposes a verified download descriptor");
    for (const auto* bad : {"https://lumedesktop.com.evil.test/x.exe", "http://lumedesktop.com/x.exe",
                            "https://user@lumedesktop.com/x.exe", "https://lumedesktop.com:443/x.exe",
                            "https://lumedesktop.com\\evil/x.exe", "https://lumedesktop.com/x.exe#fragment"})
        Check(!IsOfficialUpdateUrl(bad), "strict official URL policy");
    for (const auto* bad : {
        R"({"version":"1.0.9","version":"1.0.2.1"})", R"({"version":123})", "{", "[]",
        R"({"version":"1.0.9","runtime":"native-win32","channel":"native-verification"})"
    }) Check(EvaluateManifest(bad, "1.0.7").update.status == "error", "invalid native metadata is not current or downloadable");
    const auto sizeField = valid.find("\"size\":1024");
    for (const auto* size : {"true", "1", "1073741825", "1.0", "\"1024\""}) {
        auto invalidSize = valid;
        invalidSize.replace(sizeField, std::string("\"size\":1024").size(), std::string("\"size\":") + size);
        Check(EvaluateManifest(invalidSize, "1.0.7").update.status == "error", "installer metadata has strictly bounded integer size");
    }
    Check(EvaluateManifest(std::string(65537, ' ')).update.status == "error", "bounded manifest");
    std::cout << "application settings update policy passed\n";
}
