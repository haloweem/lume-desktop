#include <iostream>
#include "platform/windows/account/http.h"
#include <winhttp.h>
#include <cstdlib>
using namespace lume::account;
using namespace lume::platform::windows::account;
void Expect(bool ok, const char* why) { if (!ok) { std::cerr << why << '\n'; std::exit(1); } }
int main() {
    Expect(MayRetryDirect(ERROR_WINHTTP_CANNOT_CONNECT), "dead default proxy can retry direct");
    Expect(MayRetryDirect(ERROR_WINHTTP_AUTODETECTION_FAILED), "auto proxy failure");
    Expect(!MayRetryDirect(ERROR_WINHTTP_SECURE_FAILURE), "TLS validation failure not bypassed");
    Expect(!MayRetryDirect(ERROR_WINHTTP_TIMEOUT), "ambiguous POST timeout not replayed");
    WinHttpTransport http;
    Cancellation cancel;
    HttpRequest request; request.target = Target::Token; request.url = "http://evil.example"; request.method = "POST";
    bool rejected = false;
    try { http.Send(request, cancel); } catch (const Failure& e) { rejected = e.code == Error::InvalidTarget; }
    Expect(rejected, "HTTP adapter validates before touching network");
    request.url = std::string(Issuer) + "/api/login/oauth/access_token";
    cancel.Cancel(); rejected = false;
    try { http.Send(request, cancel); } catch (const Failure& e) { rejected = e.code == Error::Cancelled; }
    Expect(rejected, "pre-cancelled HTTP never sends");
    std::cout << "HTTP security policy passed without network\n";
}
