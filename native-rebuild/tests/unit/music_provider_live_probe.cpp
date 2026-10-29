#include "platform/windows/music_provider_http.h"
#include <iostream>

int main(int argc, char** argv) {
    using namespace lume::music::auth;
    const std::string_view mode = argc == 2 ? argv[1] : "";
    if (mode != "--qr-only" && mode != "--qr-trace") return 2;
    const bool trace = mode == "--qr-trace";
    // Trace prints each hop's route, HTTP status, size and the service's own
    // status/error codes; never cookies, keys or the QR challenge.
    const Exchange traced = [&](const HttpRequest& request, const Cancelled& stop) {
        const auto route = request.url.substr(0, request.url.find('?'));
        try {
            auto response = SendHttp(request, stop);
            std::cout << "  " << request.method << ' ' << route << " -> " << response.status
                << ", bytes=" << response.body.size();
            const auto body = Json::parse(response.body, nullptr, false);
            if (body.is_object())
                for (const auto* key : {"status", "error_code", "errcode", "code"})
                    if (body.contains(key) && body[key].is_number()) std::cout << ", " << key << '=' << body[key];
            std::cout << '\n';
            return response;
        } catch (const std::exception& failure) {
            std::cout << "  " << request.method << ' ' << route << " -> threw " << failure.what() << '\n';
            throw;
        }
    };
    bool passed = true;
    for (const auto provider : {Provider::Netease, Provider::QQ, Provider::Kugou}) {
        // No storage, no existing identity, no polling or account confirmation.
        Json temporary = Json::object();
        try {
            const Cancelled never = [] { return false; };
            const auto reply = !trace ? ExecuteProvider(provider, Operation::Begin, temporary, never) :
                provider == Provider::Netease ? RunNetease(Operation::Begin, temporary, never, traced) :
                provider == Provider::QQ ? RunQQ(Operation::Begin, temporary, never, traced) :
                RunKugou(Operation::Begin, temporary, never, traced);
            const bool ok = reply.status == "authorizing" && (!reply.qrImage.empty() || !reply.qrText.empty());
            std::cout << Name(provider) << ": " << (ok ? "QR available" : "QR unavailable")
                << ", image bytes=" << reply.qrImage.size() << ", text bytes=" << reply.qrText.size()
                // The error is a fixed public token such as "service", never a payload.
                << (reply.error.empty() ? "" : ", error=" + reply.error) << '\n';
            passed = passed && ok;
        } catch (...) {
            // Exceptions and bodies may contain endpoint details; only report
            // availability, never the challenge, cookies, or response payload.
            std::cout << Name(provider) << ": QR request failed\n";
            passed = false;
        }
    }
    return passed ? 0 : 1;
}
