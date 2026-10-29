#include "platform/windows/weather/weather_backend.h"
#include "platform/windows/weather/weather_http.h"
#include "platform/windows/weather/weather_timezone.h"
#include "weather_fixtures.h"
#include <windows.h>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
using namespace lume::weather;
using namespace lume::platform::windows;
using namespace weather_fixture;
namespace {
void Expect(bool value, const char* reason) {
    if (!value) { std::cerr << "FAIL: " << reason << '\n'; std::exit(1); }
}
struct Harness {
    Millis now = Now;
    std::map<WeatherCacheKind, std::string> saved;
    std::deque<HttpResponse> replies;
    int requests = 0, writes = 0;
    bool writeFailure = false;
    Cancellation cancel;
    WeatherCachePort Cache() {
        return {[&](auto kind, std::size_t max) -> std::optional<std::string> {
            const auto found = saved.find(kind);
            if (found == saved.end() || found->second.size() > max) return {};
            return found->second;
        }, [&](auto kind, std::string_view data) {
            ++writes;
            if (writeFailure) return false;
            saved[kind] = data;
            return true;
        }};
    }
    std::unique_ptr<IWeatherBackend> Backend() {
        return CreateWeatherBackend({Cache(), [&](const auto&, const Cancellation&) {
            ++requests;
            Expect(!replies.empty(), "no unexpected backend request");
            auto reply = std::move(replies.front()); replies.pop_front(); return reply;
        }, [&] { return now; }, Local, false, LocationEnvironment{}});
    }
    void Responses() {
        replies = {{200, Json{{"city", u8"蘇州市"}, {"latitude", Shanghai.latitude},
            {"longitude", Shanghai.longitude}, {"timezone", "Asia/Shanghai"}}.dump(), {}, {}, {}},
            {}, {200, MetBody().dump(), {}, {}, {}}};
    }
};
void LivePathAndPersistence() {
    Harness h; h.Responses();
    auto backend = h.Backend();
    Expect(backend != nullptr, "native backend created");
    Expect(!backend->Load("auto"), "empty cache has no artificial display");
    const auto first = backend->Fetch("auto", h.cancel);
    Expect(first.result.snapshot && first.result.snapshot->source == Source::MetNorway &&
        first.result.snapshot->cityName == u8"苏州市" && first.result.snapshot->location.name == u8"苏州市" &&
        h.requests == 3 && !first.cacheWriteFailed, "IP location to real provider parsing and Simplified Chinese output");
    Expect(h.saved.size() == 3 && h.saved.count(WeatherCacheKind::Location),
        "bounded raw and display snapshots and the trusted automatic location persisted");
    const auto beforeWrites = h.writes;
    Expect(backend->Fetch("auto", h.cancel).result.snapshot.has_value() && h.requests == 3 && h.writes == beforeWrites,
        "fresh location/provider caches avoid repeated network and identical disk writes");
    backend.reset();
    auto restored = h.Backend();
    Expect(restored->Load("auto").has_value() && !restored->Load("beijing"), "display cache restored only for matching city");
    // Automatic geolocation is deliberately not persisted; one fresh IP call,
    // public source retry, then the persisted MET cache is sufficient.
    h.replies = {{200, Json{{"city", u8"蘇州市"}, {"latitude", Shanghai.latitude},
        {"longitude", Shanghai.longitude}, {"timezone", "Asia/Shanghai"}}.dump(), {}, {}, {}}, {}};
    const auto next = restored->Fetch("auto", h.cancel);
    Expect(next.result.snapshot && h.requests == 5, "raw MET cache restores across backend lifetime without another forecast request");
    h.now += Day + 1;
    Expect(!restored->Load("auto"), "expired display never restored");
}
void FailureAndCancel() {
    Harness h; h.Responses(); h.writeFailure = true;
    auto backend = h.Backend();
    const auto fetched = backend->Fetch("auto", h.cancel);
    Expect(fetched.result.snapshot && fetched.cacheWriteFailed && h.saved.empty(),
        "disk failure preserves usable weather and exposes persistence error");
    h.cancel.Cancel();
    const auto previousRequests = h.requests, previousWrites = h.writes;
    Expect(backend->Fetch("auto", h.cancel).result.error == ProviderError::Cancelled &&
        h.requests == previousRequests && h.writes == previousWrites, "cancelled backend performs no new work");
    Harness malformed;
    malformed.saved[WeatherCacheKind::Display] = "{invalid";
    malformed.saved[WeatherCacheKind::Provider] = "{invalid";
    auto corrupt = malformed.Backend();
    Expect(!corrupt->Load("auto"), "corrupt cache does not crash or fabricate a snapshot");
}
void RealFiles() {
    const auto directory = std::filesystem::temp_directory_path() /
        ("LumeWeatherCacheTest-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    Expect(!std::filesystem::exists(directory), "test owns a new isolated directory");
    auto cache = LocalWeatherCache(directory);
    Expect(!cache.read(WeatherCacheKind::Display, 20) && !std::filesystem::exists(directory),
        "read does not create directories");
    Expect(cache.write(WeatherCacheKind::Display, "old") && cache.write(WeatherCacheKind::Display, "new"),
        "cache directory and atomic replacement");
    Expect(cache.read(WeatherCacheKind::Display, 3) == std::optional<std::string>("new") &&
        !cache.read(WeatherCacheKind::Display, 2), "disk reads enforce exact byte bound");
    Expect(cache.write(WeatherCacheKind::Provider, std::string(10000, 'x')) &&
        !cache.read(WeatherCacheKind::Provider, 9999), "large cache bounded");
    int files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        Expect(entry.is_regular_file() && entry.path().extension() == ".json", "no leftover temporary cache files");
        ++files;
        Expect(std::filesystem::remove(entry.path()), "remove owned test cache");
    }
    Expect(files == 2 && std::filesystem::remove(directory), "test cleans only its own two files and empty directory");
}
}
int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--live-auto") {
        // Explicit network diagnostic only. No installed preferences or cache
        // path is read/written, and IP response bodies are never logged.
        // "--live-auto ip" disables the system (Wi-Fi) fix to test the IP path alone.
        WeatherBackendOptions options;
        options.systemLocation = !(argc == 3 && std::string(argv[2]) == "ip");
        auto backend = CreateWeatherBackend(std::move(options));
        Cancellation cancel;
        const auto live = backend->Fetch("auto", cancel);
        if (!live.result.snapshot) {
            std::cout << "automatic weather unavailable; error=" << static_cast<int>(live.result.error) << '\n';
            return 1;
        }
        const auto& snapshot = *live.result.snapshot;
        std::cout << "automatic weather source=" << (snapshot.source == Source::Qweather ? "qweather" : "met-norway")
            << " city=" << snapshot.cityName << " temperature=" << snapshot.temperature
            << " sourceUpdatedAt=" << snapshot.sourceUpdatedAt << " fetchedAt=" << snapshot.updatedAt
            << " hours=" << snapshot.hourly.size() << " days=" << snapshot.forecast.size() << '\n';
        return 0;
    }
    if (argc == 2 && std::string(argv[1]) == "--live-met") {
        WeatherHttp http;
        WeatherTimezone timezone;
        Cancellation cancel;
        HttpRequest request;
        request.url = "https://api.met.no/weatherapi/locationforecast/2.0/compact?lat=31.2304&lon=121.4737";
        request.headers = {{"User-Agent", "Lume/1.0.3.3 (Windows desktop weather)"}, {"Accept", "application/json"}};
        request.timeoutMs = 12000;
        const auto response = http.Get(request, cancel);
        const auto now = WeatherNow();
        const auto parsed = ParseMet(response.body, *NamedLocation("shanghai"), "shanghai", now, now,
            [&](Millis time, std::string_view zone) { return timezone.Local(time, zone); });
        std::cout << "MET live status=" << response.status << " bytes=" << response.body.size()
            << " parsed=" << parsed.has_value() << '\n';
        if (!parsed || (response.status != 200 && response.status != 203)) return 1;
        std::cout << "MET temperature=" << parsed->temperature << " hours=" << parsed->hourly.size()
            << " days=" << parsed->forecast.size() << " sourceUpdatedAt=" << parsed->sourceUpdatedAt << '\n';
        return 0;
    }
    LivePathAndPersistence(); FailureAndCancel(); RealFiles();
    Expect(WeatherNow() > 1700000000000LL, "native clock returns UTC milliseconds");
    std::cout << "native weather backend, bounded atomic persistence and real data pipeline passed\n";
}
