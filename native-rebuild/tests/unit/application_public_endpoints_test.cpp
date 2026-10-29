#include "platform/windows/account/http.h"
#include "platform/windows/application_settings/application_settings_ports.h"
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2 || std::string(argv[1]) != "--public-readonly") return 2;
    namespace account = lume::account;
    namespace settings = lume::application_settings;
    try {
        lume::platform::windows::account::WinHttpTransport transport;
        account::Cancellation cancellation;
        account::HttpRequest request;
        request.url = std::string(account::Issuer) + "/.well-known/openid-configuration";
        const auto discoveryResponse = transport.Send(request, cancellation);
        const auto discovery = account::StrictJson(discoveryResponse.body);
        if (discoveryResponse.status != 200 || discovery.value("issuer", "") != account::Issuer ||
            discovery.value("authorization_endpoint", "") != std::string(account::Issuer) + "/login/oauth/authorize" ||
            discovery.value("token_endpoint", "") != std::string(account::Issuer) + "/api/login/oauth/access_token" ||
            discovery.value("userinfo_endpoint", "") != std::string(account::Issuer) + "/api/userinfo" ||
            discovery.value("jwks_uri", "") != std::string(account::Issuer) + "/.well-known/jwks") return 1;
        std::cout << "PASS: native HTTPS discovery matches the configured account endpoints\n";
        request.target = account::Target::Jwks;
        request.url = std::string(account::Issuer) + "/.well-known/jwks";
        const auto keysResponse = transport.Send(request, cancellation);
        const auto keys = account::StrictJson(keysResponse.body);
        bool usable = false;
        if (keysResponse.status != 200 || !keys.contains("keys") || !keys["keys"].is_array()) return 1;
        for (const auto& key : keys["keys"]) {
            if (key.value("kty", "") == "RSA" && key.value("use", "") == "sig" &&
                (key.value("alg", "") == "RS256" || key.value("alg", "") == "RS512") &&
                !key.value("kid", "").empty() && key.contains("n") && key.contains("e")) usable = true;
        }
        if (!usable) return 1;
        std::cout << "PASS: native HTTPS returns a supported public signing key\n";
        settings::Cancellation updateCancellation;
        const auto manifest = lume::platform::windows::CreateApplicationUpdateTransport(false)->Fetch(updateCancellation);
        if (!manifest.error.empty()) {
            std::cerr << "FAIL: update manifest " << manifest.error << '\n';
            return 1;
        }
        const auto update = settings::EvaluateManifest(manifest.body);
        if (update.update.status != "current" && update.update.status != "incompatible") return 1;
        std::cout << "PASS: native HTTPS public update manifest version=" << update.update.version
                  << " policy=" << update.update.status << " error=" << update.error << '\n';
        return 0;
    } catch (const account::Failure& failure) {
        std::cerr << "FAIL: native public account endpoint " << account::ErrorCode(failure.code) << '\n';
        return 1;
    } catch (...) {
        std::cerr << "FAIL: unexpected public endpoint response\n";
        return 1;
    }
}
