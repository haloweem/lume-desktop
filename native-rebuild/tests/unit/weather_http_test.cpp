#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "platform/windows/weather/weather_http.h"
#include "platform/windows/weather/weather_timezone.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
using namespace lume::weather;
using lume::platform::windows::WeatherHttp;
using namespace std::chrono_literals;
namespace {
void Expect(bool value, const char* reason) {
    if (!value) { std::cerr << "FAIL: " << reason << " (" << GetLastError() << ")\n"; std::exit(1); }
}
// Only the fixture can opt into HTTP loopback. Production requests remain fixed HTTPS origins.
class Server {
public:
    explicit Server(std::string reply, int delayMs = 0) {
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        Expect(listener_ != INVALID_SOCKET, "fixture socket");
        sockaddr_in address{}; address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        Expect(bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 &&
            listen(listener_, 1) == 0, "fixture listener");
        int length = sizeof(address);
        getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length);
        port_ = ntohs(address.sin_port);
        thread_ = std::thread([this, reply = std::move(reply), delayMs] {
            auto client = accept(listener_, nullptr, nullptr);
            if (client == INVALID_SOCKET) return;
            char buffer[4096];
            DWORD timeout = 3000;
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
            const auto count = recv(client, buffer, sizeof(buffer), 0);
            if (count > 0) request_.assign(buffer, static_cast<std::size_t>(count));
            received.store(true);
            for (int i = 0; i < delayMs && !stopping_.load(); i += 10) std::this_thread::sleep_for(10ms);
            std::size_t offset = 0;
            while (offset < reply.size() && !stopping_.load()) {
                const auto sent = send(client, reply.data() + offset,
                    static_cast<int>(std::min<std::size_t>(16384, reply.size() - offset)), 0);
                if (sent <= 0) break;
                offset += static_cast<std::size_t>(sent);
            }
            shutdown(client, SD_BOTH); closesocket(client);
        });
    }
    ~Server() { stopping_.store(true); closesocket(listener_); if (thread_.joinable()) thread_.join(); }
    std::string Url() const { return "http://127.0.0.1:" + std::to_string(port_) + "/weather"; }
    void Join() { if (thread_.joinable()) thread_.join(); }
    std::string request_;
    std::atomic<bool> received{false};
private:
    SOCKET listener_ = INVALID_SOCKET;
    unsigned short port_ = 0;
    std::atomic<bool> stopping_{false};
    std::thread thread_;
};
std::string Response(std::string body, std::string headers = {}) {
    return "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) +
        "\r\nConnection: close\r\n" + headers + "\r\n" + body;
}
void Transfer() {
    Server server(Response(u8"真实UTF-8正文", "Expires: Thu, 17 Sep 2026 04:00:00 GMT\r\n"
        "Last-Modified: Thu, 17 Sep 2026 01:00:00 GMT\r\n"));
    HttpRequest request; request.url = server.Url();
    request.headers = {{"If-Modified-Since", "Thu, 17 Sep 2026 01:00:00 GMT"}};
    Cancellation cancel;
    const auto result = WeatherHttp(true).Get(request, cancel);
    Expect(result.status == 200 && result.body == u8"真实UTF-8正文" &&
        result.expires == "Thu, 17 Sep 2026 04:00:00 GMT" &&
        result.lastModified == "Thu, 17 Sep 2026 01:00:00 GMT",
        "async WinHTTP reads bounded UTF-8 body and conditional response metadata");
    server.Join();
    Expect(server.request_.find("If-Modified-Since: Thu, 17 Sep 2026 01:00:00 GMT") != std::string::npos,
        "conditional cache header reaches actual socket");
}
void BoundedAndChunked() {
    Cancellation cancel;
    {
        Server server("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
            "4\r\nrain\r\n3\r\nbow\r\n0\r\n\r\n");
        HttpRequest request; request.url = server.Url();
        auto result = WeatherHttp(true).Get(request, cancel);
        Expect(result.status == 200 && result.body == "rainbow", "chunked body decoded");
    }
    {
        // Deterministic gzip stream for 'hello', generated once as test data.
        const unsigned char bytes[] = {31,139,8,0,0,0,0,0,0,3,203,72,205,201,201,7,0,134,166,16,54,5,0,0,0};
        Server server(Response(std::string(reinterpret_cast<const char*>(bytes), sizeof(bytes)), "Content-Encoding: gzip\r\n"));
        HttpRequest request; request.url = server.Url();
        auto result = WeatherHttp(true).Get(request, cancel);
        Expect(result.status == 200 && result.body == "hello", "native gzip decompression without extra runtime");
    }
    {
        Server server(Response(std::string(10000, 'x')));
        HttpRequest request; request.url = server.Url(); request.maxBytes = 100;
        Expect(WeatherHttp(true).Get(request, cancel).status == 0, "oversized decoded response rejected");
    }
    {
        Server server("HTTP/1.1 429 Too Many Requests\r\nRetry-After: 3600\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        HttpRequest request; request.url = server.Url();
        auto result = WeatherHttp(true).Get(request, cancel);
        Expect(result.status == 429 && result.retryAfter == "3600", "rate limit metadata delivered without fake success");
    }
    {
        Server server("HTTP/1.1 200 OK\r\nContent-Length: 100\r\nConnection: close\r\n\r\nshort");
        HttpRequest request; request.url = server.Url();
        Expect(WeatherHttp(true).Get(request, cancel).status == 0, "interrupted body cannot become valid page");
    }
}
void CancellationAndDeadline() {
    {
        Server server(Response("late"), 3000);
        Cancellation cancel;
        HttpRequest request; request.url = server.Url(); request.timeoutMs = 5000;
        HttpResponse result;
        std::thread worker([&] { result = WeatherHttp(true).Get(request, cancel); });
        const auto until = std::chrono::steady_clock::now() + 3s;
        while (!server.received.load() && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(5ms);
        const auto start = std::chrono::steady_clock::now();
        cancel.Cancel(); worker.join();
        Expect(result.status == 0 && std::chrono::steady_clock::now() - start < 500ms,
            "in-flight cancellation returns without waiting for response timeout");
    }
    {
        Server server(Response("late"), 3000);
        Cancellation cancel;
        HttpRequest request; request.url = server.Url(); request.timeoutMs = 150;
        const auto start = std::chrono::steady_clock::now();
        auto result = WeatherHttp(true).Get(request, cancel);
        Expect(result.status == 0 && std::chrono::steady_clock::now() - start < 750ms,
            "total request deadline bounds headers wait");
    }
}
void OriginAndHeaders() {
    Cancellation cancel;
    for (const auto& url : {"http://127.0.0.1:9/weather", "http://www.qweather.com/weather/x.html",
        "https://evil.test/x", "https://user:pass@www.qweather.com/v2/search",
        "https://www.qweather.com:444/v2/search", "https://www.qweather.com/not-weather",
        "https://api.met.no/weatherapi/locationforecast/2.0/compact#fragment"}) {
        HttpRequest request; request.url = url;
        Expect(WeatherHttp().Get(request, cancel).status == 0, "unsupported origins rejected before network");
    }
    HttpRequest header; header.url = "https://www.qweather.com/v2/search";
    header.headers = {{"Accept", "text/html\r\nInjected: true"}};
    Expect(WeatherHttp().Get(header, cancel).status == 0, "header injection rejected before network");
    Cancellation stopped; stopped.Cancel();
    header.headers.clear();
    Expect(WeatherHttp().Get(header, stopped).status == 0, "pre-cancelled HTTP request does nothing");
}
}
int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--live") {
        // Explicit opt-in diagnostic; not part of hermetic CTest or packaging.
        Cancellation cancel;
        WeatherHttp transport;
        HttpRequest request;
        request.url = "https://www.qweather.com/v2/search?lng=120.9812&lat=31.3851";
        request.headers = {{"Accept", "application/json"}};
        const auto result = transport.Get(request, cancel);
        std::cout << "public-search status=" << result.status << " bytes=" << result.body.size() <<
            " body=" << result.body.substr(0, 250) << '\n';
        lume::platform::windows::WeatherTimezone zone;
        WeatherProvider provider(
            [&](const HttpRequest& r, const Cancellation& c) {
                const auto reply = transport.Get(r, c);
                std::cout << "provider status=" << reply.status << " bytes=" << reply.body.size() << '\n';
                return reply;
            },
            [] { return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count(); },
            [&](Millis time, std::string_view name) { return zone.Local(time, name); });
        const auto weather = provider.Fetch("shanghai", *NamedLocation("shanghai"), cancel);
        if (!weather.snapshot) {
            std::cout << "provider result error=" << static_cast<int>(weather.error) << '\n';
            return 1;
        }
        const auto& snapshot = *weather.snapshot;
        std::cout << "source=" << (snapshot.source == Source::Qweather ? "qweather" : "met-norway") <<
            " sourceUpdatedAt=" << snapshot.sourceUpdatedAt << " fetchedAt=" << snapshot.updatedAt <<
            " temperature=" << snapshot.temperature << " hours=" << snapshot.hourly.size() <<
            " days=" << snapshot.forecast.size() << '\n';
        return 0;
    }
    WSADATA data{};
    Expect(WSAStartup(MAKEWORD(2,2), &data) == 0, "Winsock test startup");
    Transfer(); BoundedAndChunked(); CancellationAndDeadline(); OriginAndHeaders();
    WSACleanup();
    std::cout << "native weather HTTP bounds, encoding, cancellation, deadlines and origin checks passed\n";
}
