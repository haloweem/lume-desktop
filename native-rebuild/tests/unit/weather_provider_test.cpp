#include "core/weather/weather_provider.h"
#include "weather_fixtures.h"
#include <cstdlib>
#include <deque>
#include <iostream>
#include <stdexcept>
using namespace lume::weather;
using namespace weather_fixture;
namespace {
void Expect(bool value, const char* reason) {
    if (!value) { std::cerr << "FAIL: " << reason << '\n'; std::exit(1); }
}
const auto PublicNow = *ParseUtc("2026-09-21T18:35:00Z");
const std::string Search = R"({"success":true,"data":"kunshan-101190404"})";
HttpResponse Reply(int status, std::string body = {}, std::string retry = {},
    std::string expires = {}, std::string modified = {}) {
    return {status, std::move(body), std::move(retry), std::move(expires), std::move(modified)};
}
struct Harness {
    Millis now = PublicNow;
    std::deque<HttpResponse> replies;
    std::vector<HttpRequest> requests;
    std::function<void()> duringRequest;
    WeatherProvider provider{
        [this](const HttpRequest& request, const Cancellation&) {
            requests.push_back(request);
            Expect(request.maxBytes == ProviderMaxBytes, "every provider request has a body bound");
            if (duringRequest) duringRequest();
            Expect(!replies.empty(), "no unexpected additional request");
            auto result = std::move(replies.front()); replies.pop_front(); return result;
        }, [this] { return now; }, Local};
    Cancellation cancel;
    ProviderResult Fetch(std::string_view city = "auto", const Location& location = Shanghai) {
        return provider.Fetch(city, location, cancel);
    }
};
std::string Header(const HttpRequest& request, std::string_view key) {
    for (const auto& pair : request.headers) if (pair.first == key) return pair.second;
    return {};
}
void Dates() {
    Expect(ParseHttpDate("Thu, 17 Sep 2026 03:35:00 GMT") == Now,
        "HTTP expiry and Retry-After dates decode as UTC");
    Expect(!ParseHttpDate("Thu, 30 Feb 2026 03:35:00 GMT") &&
        !ParseHttpDate("Thu, 17 Sep 2026 03:35:00 PST") &&
        !ParseHttpDate("Thu, 17 Sep 2026 03:35:00 GMT\r\nInjected: yes"),
        "HTTP dates cannot carry invalid dates or header injection");
}
void PublicCache() {
    Harness h;
    h.replies = {Reply(200, Search), Reply(200, PublicPage())};
    const auto first = h.Fetch();
    Expect(first.snapshot && first.error == ProviderError::None &&
        first.snapshot->source == Source::Qweather &&
        first.snapshot->sourceUrl == "https://www.qweather.com/weather/kunshan-101190404.html",
        "public provider searches then returns attributed observation");
    Expect(h.requests.size() == 2 && h.requests[0].url ==
        "https://www.qweather.com/v2/search?lng=120.9812&lat=31.3851" &&
        h.requests[1].timeoutMs == 10000, "original coordinates and public timeout");
    h.now += 60000;
    auto reused = h.Fetch("shanghai");
    Expect(reused.snapshot && reused.snapshot->cityId == "shanghai" &&
        reused.snapshot->updatedAt == PublicNow && h.requests.size() == 2,
        "coordinate cache retags caller city without inventing a download");
    const auto saved = h.provider.SaveCache();
    Expect(saved.has_value(), "provider pages can persist independently from display snapshot");
    Harness restored; restored.now = h.now; restored.provider.RestoreCache(*saved);
    Expect(restored.Fetch().snapshot.has_value() && restored.requests.empty(),
        "restored valid raw page avoids network and is parsed again");
    h.now = PublicNow + 10 * 60000;
    h.replies = {Reply(200, PublicPage(3, "2026-09-22", "02:40"))};
    auto refreshed = h.Fetch();
    Expect(refreshed.snapshot && refreshed.snapshot->updatedAt == h.now &&
        h.requests.size() == 3 && h.requests.back().url.find("/weather/") != std::string::npos,
        "ten minute expiry reuses resolved slug but downloads a new page");
}
void FallbackAndRevalidation() {
    Harness h; h.now = Now;
    h.replies = {Reply(403, {}, "7200"), Reply(200, MetBody().dump(), {},
        "Thu, 17 Sep 2026 03:36:00 GMT", "Thu, 17 Sep 2026 01:00:00 GMT")};
    auto first = h.Fetch();
    Expect(first.snapshot && first.snapshot->source == Source::MetNorway &&
        first.snapshot->remainingDay && h.requests.size() == 2,
        "public denial immediately falls back to real forecast");
    Expect(h.requests.back().url ==
        "https://api.met.no/weatherapi/locationforecast/2.0/compact?lat=31.3851&lon=120.9812" &&
        h.requests.back().timeoutMs == 12000 &&
        !Header(h.requests.back(), "User-Agent").empty(), "MET request identity, precision and timeout");
    h.now += 30000;
    Expect(h.Fetch().snapshot.has_value() && h.requests.size() == 2, "MET Expires prevents repeated download");
    h.now = Now + 2 * 60000;
    h.replies = {Reply(304, {}, {}, "Thu, 17 Sep 2026 06:00:00 GMT")};
    const auto validated = h.Fetch();
    Expect(validated.snapshot && validated.snapshot->updatedAt == h.now &&
        validated.snapshot->sourceUpdatedAt == first.snapshot->sourceUpdatedAt &&
        Header(h.requests.back(), "If-Modified-Since") == "Thu, 17 Sep 2026 01:00:00 GMT",
        "304 revalidates original data and retains its source timestamp");
    h.now = Now + 90 * 60000;
    Expect(h.Fetch().snapshot.has_value() && h.requests.size() == 3,
        "public Retry-After longer than one hour is respected while MET remains usable");
    h.now = Now + 2 * Hour;
    h.replies = {Reply(503)};
    Expect(h.Fetch().snapshot.has_value() && h.requests.size() == 4 &&
        h.requests.back().url.find("qweather.com") != std::string::npos,
        "public provider retried when its backoff ends");
}
void FailuresAndCancellation() {
    Harness h; h.now = Now;
    h.replies = {Reply(500), Reply(429, {}, "120")};
    auto failed = h.Fetch();
    Expect(!failed.snapshot && failed.retryAt == Now + 120000, "MET Retry-After gives a real retry deadline");
    h.now += 60000;
    auto delayed = h.Fetch();
    Expect(!delayed.snapshot && delayed.error == ProviderError::RetryDelayed &&
        h.requests.size() == 2, "no network during provider backoff");
    h.now += 60000;
    h.replies = {Reply(304)};
    Expect(!h.Fetch().snapshot && h.requests.size() == 3, "304 without an existing body cannot invent weather");
    Harness before; before.cancel.Cancel();
    Expect(before.Fetch().error == ProviderError::Cancelled && before.requests.empty(),
        "already cancelled refresh performs no network");
    Harness during;
    during.replies = {Reply(200, Search)};
    during.duringRequest = [&] { during.cancel.Cancel(); };
    Expect(during.Fetch().error == ProviderError::Cancelled && during.requests.size() == 1 &&
        during.provider.CachedEntries() == 0, "cancelled location search cannot launch a page or fallback");
    Harness page;
    page.replies = {Reply(200, Search), Reply(200, PublicPage())};
    page.duringRequest = [&] { if (page.requests.size() == 2) page.cancel.Cancel(); };
    Expect(page.Fetch().error == ProviderError::Cancelled && page.provider.CachedEntries() == 0,
        "late network completion after cancellation never enters cache");
    page.duringRequest = {};
    page.replies = {Reply(200, Search), Reply(200, PublicPage())};
    Cancellation next;
    Expect(page.provider.Fetch("auto", Shanghai, next).snapshot.has_value(),
        "cancellation does not poison next enable with provider backoff");
    Harness invalid;
    Expect(invalid.Fetch("not-a-city").error == ProviderError::InvalidLocation &&
        invalid.Fetch("auto", {999,0,"",""}).error == ProviderError::InvalidLocation &&
        invalid.requests.empty(), "invalid locations never reach network");
}
void MalformedAndBounds() {
    for (const auto& search : {std::string(R"({"success":true,"data":"https://evil.test/x"})"),
        std::string(R"({"success":true,"data":"kunshan-101190404/../../x"})"),
        std::string(R"({"success":true,"data":"kunshan--101190404"})"),
        std::string(ProviderMaxBytes + 1, 'x')}) {
        Harness h; h.now = Now;
        h.replies = {Reply(200, search), Reply(200, MetBody().dump())};
        Expect(h.Fetch().snapshot.has_value() && h.requests.size() == 2 &&
            h.requests.back().url.find("api.met.no") != std::string::npos,
            "untrusted search slugs and oversized bodies go to fixed fallback, never arbitrary URL");
    }
    Harness h;
    for (int i = 0; i < 5; ++i) {
        auto location = Shanghai; location.latitude += i;
        h.replies = {Reply(200, Search), Reply(200, PublicPage())};
        Expect(h.Fetch("auto", location).snapshot.has_value(), "separate location page accepted");
    }
    Expect(h.provider.CachedEntries() == 4 &&
        h.provider.CachedBodyBytes() == 4 * PublicPage().size(),
        "raw public page cache evicts oldest at four entries");
    const auto saved = *h.provider.SaveCache();
    Harness restored; restored.provider.RestoreCache(saved);
    Expect(restored.provider.CachedEntries() == 4, "disk restore cannot grow bounded caches");
    restored.provider.RestoreCache("{malformed");
    Expect(restored.provider.CachedEntries() == 4, "damaged disk data does not replace good in-memory cache");
    Harness rollback; rollback.now = PublicNow - 1; rollback.provider.RestoreCache(saved);
    Expect(rollback.provider.CachedEntries() == 0, "future caches rejected on restore after clock rollback");
    auto bad = Json::parse(saved);
    bad["version"] = 999;
    Harness unknown; unknown.provider.RestoreCache(bad.dump());
    Expect(unknown.provider.CachedEntries() == 0, "unknown cache versions are ignored");
}
}
int main() {
    Dates(); PublicCache(); FallbackAndRevalidation(); FailuresAndCancellation(); MalformedAndBounds();
    std::cout << "weather provider fallback, conditional cache, backoff, cancellation and bounds passed\n";
}
